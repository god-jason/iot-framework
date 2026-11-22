/*
 * bootloader.c - STM32F1（128KB Flash）单 App 槽 + OTA 进度日志 Bootloader
 *
 * 分区（详见 partition.h）：
 *   Boot 8K / 配置 4K×2 / 系统日志 4K / 升级日志 4K / App 52K / 升级包 52K
 *
 * 升级流程：
 *   1. 上位机经 UART1 发送新固件（按 App 区 0x08008000 链接），Bootloader
 *      写入升级包区，校验 CRC 后在升级日志区追加 READY 记录（升级有效标识），
 *      随后复位；
 *   2. 下次启动读取最后一条进度记录：READY 则先擦除 App 区并置 APPLYING，
 *      再按 1KB 分块把升级包拷入 App 区；
 *   3. 每拷完一块追加一条进度记录（已写字节数）；掉电重启后从该偏移继续，
 *      保证 App 区最终是完整镜像（不会启动半写入的程序）；
 *   4. 全部写完并整段 CRC 校验通过后，擦除升级日志分区（清除有效标识），
 *      再启动 App。
 *
 * 不再使用 A/B 双槽，也没有回滚：唯一保障是“进度日志 + CRC 校验”。
 *
 * 不依赖 HAL / RTOS / libc，限制在 Flash 前 8KB 内。
 */

#include "boot_meta.h"

#define UART_BRR_8MHZ_115200    69U     /* 8MHz / 115200 ≈ 69.44，误差 0.6% */
#define UPDATE_BYTE_TIMEOUT_MS  3000U
#define UPDATE_HANDSHAKE_MS     500U
#define BOOT_ACK_CHAR           0x06U
#define BOOT_NAK_CHAR           0x15U
#define OTA_CHUNK_SIZE          1024U
#define OTA_APPLY_RETRY         3U

uint32_t SystemCoreClock = 8000000U;

static volatile uint32_t s_ms;
static uint8_t s_chunk[OTA_CHUNK_SIZE];

void SystemInit(void)
{
}

void SysTick_Handler(void)
{
    s_ms++;
}

void SVC_Handler(void)
{
    for (;;) {
    }
}

void PendSV_Handler(void)
{
    for (;;) {
    }
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = s_ms;

    while ((s_ms - start) < ms) {
    }
}

/* ---------------------------- UART1 ---------------------------- */

static void uart_init(void)
{
    RCC->APB2ENR |= (1UL << 2) | (1UL << 14);       /* IOPAEN | USART1EN */

    GPIOA->CRH &= ~(0x0FUL << 4);
    GPIOA->CRH |= (0x0BUL << 4);
    GPIOA->CRH &= ~(0x0FUL << 8);
    GPIOA->CRH |= (0x08UL << 8);
    GPIOA->ODR |= (1UL << 10);

    USART1->BRR = UART_BRR_8MHZ_115200;
    USART1->CR1 = (1UL << 2) | (1UL << 3) | (1UL << 13);  /* RE|TE|UE */
}

static int uart_getc(uint32_t timeout_ms)
{
    uint32_t start = s_ms;

    for (;;) {
        if ((USART1->SR & (1UL << 5)) != 0U) {        /* RXNE */
            return (int)(USART1->DR & 0xFFU);
        }
        if (timeout_ms != 0U && (s_ms - start) >= timeout_ms) {
            return -1;
        }
    }
}

static void uart_putc(uint8_t c)
{
    while ((USART1->SR & (1UL << 7)) == 0U) {          /* TXE */
    }
    USART1->DR = c;
}

static void uart_flush(void)
{
    while ((USART1->SR & (1UL << 6)) == 0U) {          /* TC */
    }
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc((uint8_t)*s++);
    }
}

static void uart_put_hex32(uint32_t v)
{
    int shift;

    uart_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        uint32_t d = (v >> (uint32_t)shift) & 0x0FU;

        uart_putc((uint8_t)(d < 10U ? ('0' + d) : ('A' + d - 10U)));
    }
}

static void uart_put_dec(uint32_t v)
{
    char buf[10];
    uint32_t i = 0U;

    if (v == 0U) {
        uart_putc('0');
        return;
    }
    while (v > 0U && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (i > 0U) {
        uart_putc((uint8_t)buf[--i]);
    }
}

/* 打印系统日志尾部（最多 1KB） */
static void print_system_log(void)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)BOOT_LOG_BASE;
    uint32_t end = 0U;
    uint32_t start;
    uint32_t i;

    while (end < BOOT_LOG_SIZE && p[end] != 0xFFU) {
        end++;
    }
    if (end == 0U) {
        return;
    }
    start = (end > 1024U) ? (end - 1024U) : 0U;
    while (start < end && p[start] != '\n') {
        start++;
    }
    if (start < end) {
        start++;
    }

    uart_puts("\r\n[BOOT] system log (tail):\r\n");
    for (i = start; i < end; i++) {
        uint8_t c = p[i];

        if ((c >= 0x20U && c < 0x7FU) || c == '\r' || c == '\n') {
            uart_putc(c);
        }
    }
    uart_puts("\r\n");
}

static bool wait_update_req(uint32_t ms)
{
    uint32_t start = s_ms;

    while ((s_ms - start) < ms) {
        int c = uart_getc(ms - (s_ms - start));

        if (c < 0) {
            break;
        }
        if (c == 'R' || c == 'r') {
            return true;
        }
    }
    return false;
}

static int uart_read_u32(uint32_t *out)
{
    uint32_t v = 0U;
    uint32_t i;

    for (i = 0U; i < 4U; i++) {
        int c = uart_getc(UPDATE_BYTE_TIMEOUT_MS);

        if (c < 0) {
            return -1;
        }
        v |= (uint32_t)(uint8_t)c << (i * 8U);
    }
    *out = v;
    return 0;
}

/* ---------------------------- 镜像操作 ---------------------------- */

static bool image_valid(uint32_t base)
{
    uint32_t sp = *(volatile uint32_t *)(uintptr_t)base;
    uint32_t pc = *(volatile uint32_t *)(uintptr_t)(base + 4U);

    if (sp < 0x20000000U || sp > 0x20005000U) {
        return false;
    }
    return (pc >= base) && (pc < (base + BOOT_APP_SIZE));
}

static bool ota_image_crc_ok(const boot_ota_rec_t *r)
{
    if (r->len == 0U || r->len > BOOT_OTA_SIZE) {
        return false;
    }
    return boot_crc32((const void *)(uintptr_t)BOOT_OTA_BASE, r->len) == r->crc;
}

static int copy_chunk(uint32_t off, uint32_t n)
{
    uint32_t i;

    for (i = 0U; i < n; i++) {
        s_chunk[i] = *(const volatile uint8_t *)(uintptr_t)(BOOT_OTA_BASE + off + i);
    }
    return boot_flash_write(BOOT_APP_BASE + off, s_chunk, n);
}

/*
 * 把升级包写入 App 区。
 * READY：先整区擦除 App 区，再置 APPLYING/offset=0；
 * APPLYING：从 offset 继续拷贝（掉电恢复点）。
 * 每块拷完保存进度；全部完成后校验整段 CRC，成功则擦除升级日志分区。
 */
static bool ota_apply(boot_ota_rec_t *r)
{
    uint32_t attempt;
    uint32_t off;
    uint32_t n;

    for (attempt = 0U; attempt < OTA_APPLY_RETRY; attempt++) {
        if (r->state != BOOT_OTA_STATE_APPLYING || r->offset > r->len ||
            (r->offset % BOOT_FLASH_PAGE) != 0U) {
            uart_puts("[BOOT] erase app area\r\n");
            if (boot_flash_erase(BOOT_APP_BASE, BOOT_APP_SIZE) != 0) {
                return false;
            }
            r->state = BOOT_OTA_STATE_APPLYING;
            r->offset = 0U;
            if (boot_ota_save(r) != 0) {
                return false;
            }
        }

        off = r->offset;
        uart_puts("[BOOT] writing app");
        while (off < r->len) {
            n = r->len - off;
            if (n > OTA_CHUNK_SIZE) {
                n = OTA_CHUNK_SIZE;
            }
            if (copy_chunk(off, n) != 0) {
                uart_puts("\r\n[BOOT] copy failed\r\n");
                return false;
            }
            off += n;
            r->offset = off;
            if (boot_ota_save(r) != 0) {
                return false;
            }
            if ((off % (8U * 1024U)) == 0U) {
                uart_putc('.');
            }
        }
        uart_puts("\r\n");

        if (boot_crc32((const void *)(uintptr_t)BOOT_APP_BASE, r->len) == r->crc) {
            (void)boot_flash_erase(BOOT_OTA_LOG_BASE, BOOT_OTA_LOG_SIZE);
            uart_puts("[BOOT] upgrade complete\r\n");
            return true;
        }
        uart_puts("[BOOT] verify failed, retry\r\n");
        r->state = BOOT_OTA_STATE_READY;
        r->offset = 0U;
        (void)boot_ota_save(r);
    }
    return false;
}

static void jump_to(uint32_t base)
{
    uint32_t sp = *(volatile uint32_t *)(uintptr_t)base;
    uint32_t pc = *(volatile uint32_t *)(uintptr_t)(base + 4U);

    SysTick->CTRL = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = (1UL << 25);

    SCB->VTOR = base;

    __asm volatile ("cpsid i" ::: "memory");
    __asm volatile ("msr msp, %0" :: "r" (sp));
    __asm volatile ("msr control, %0" :: "r" (0));
    __asm volatile ("isb");
    __asm volatile ("cpsie i" ::: "memory");
    ((void (*)(void))(uintptr_t)pc)();

    for (;;) {
    }
}

/* ---------------------------- 串口升级（写入升级包区） ---------------------------- */

static void do_update(void)
{
    uint32_t len = 0U;
    uint32_t crc = 0U;
    uint32_t got = 0U;
    uint32_t calc = 0xFFFFFFFFU;
    uint8_t buf[64];
    uint32_t n = 0U;
    uint32_t next_dot = 4096U;
    boot_ota_rec_t rec;

    uart_puts("[BOOT] OTA request, target area OTA\r\n");
    uart_putc(BOOT_ACK_CHAR);

    if (uart_read_u32(&len) != 0 || uart_read_u32(&crc) != 0 ||
        len == 0U || len > BOOT_OTA_SIZE) {
        uart_puts("[BOOT] OTA error: bad header\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }
    uart_puts("[BOOT] OTA len=");
    uart_put_hex32(len);
    uart_puts(" crc=");
    uart_put_hex32(crc);
    uart_puts("\r\n");

    /* 先清掉旧的升级标识，避免旧记录指向正在擦除的镜像 */
    (void)boot_flash_erase(BOOT_OTA_LOG_BASE, BOOT_OTA_LOG_SIZE);
    if (boot_flash_erase(BOOT_OTA_BASE, len) != 0) {
        uart_puts("[BOOT] OTA error: erase failed\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }
    uart_puts("[BOOT] OTA erase done, receiving");
    uart_putc(BOOT_ACK_CHAR);

    while (got < len) {
        int c = uart_getc(UPDATE_BYTE_TIMEOUT_MS);

        if (c < 0) {
            uart_puts("\r\n[BOOT] OTA error: rx timeout\r\n");
            uart_putc(BOOT_NAK_CHAR);
            return;
        }
        buf[n++] = (uint8_t)c;
        got++;
        if (n == sizeof(buf) || got == len) {
            calc = boot_crc32_update(calc, buf, n);
            if (boot_flash_write(BOOT_OTA_BASE + got - n, buf, n) != 0) {
                uart_puts("\r\n[BOOT] OTA error: flash write\r\n");
                uart_putc(BOOT_NAK_CHAR);
                return;
            }
            n = 0U;
        }
        if (got >= next_dot) {
            uart_putc('.');
            next_dot += 4096U;
        }
    }
    uart_puts("\r\n");

    if ((calc ^ 0xFFFFFFFFU) != crc) {
        uart_puts("[BOOT] OTA error: crc mismatch\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }

    /* 升级有效标识：READY 记录，下次启动据此开始/继续写入 App 区 */
    rec.magic = BOOT_OTA_LOG_MAGIC;
    rec.seq = 0U;
    rec.state = BOOT_OTA_STATE_READY;
    rec.offset = 0U;
    rec.len = len;
    rec.crc = crc;
    rec.reserved = 0U;
    rec.rec_crc = 0U;
    if (boot_ota_save(&rec) != 0) {
        uart_puts("[BOOT] OTA error: log save\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }

    uart_puts("[BOOT] OTA ok, rebooting\r\n");
    uart_putc(BOOT_ACK_CHAR);
    uart_flush();
    delay_ms(20U);
    NVIC_SystemReset();
}

/* ---------------------------- main ---------------------------- */

int main(void)
{
    boot_ota_rec_t rec;

    (void)SysTick_Config(8000U);                    /* 8MHz / 1000 = 1ms */
    uart_init();

    uart_puts("\r\n[BOOT] f103cb single-slot OTA\r\n");
    print_system_log();

    /* 配置区校验：主区无效且备份有效时自动还原 */
    {
        int cfg = boot_config_restore();

        if (cfg == 1) {
            uart_puts("[BOOT] config invalid, restored from backup\r\n");
        } else if (cfg == -2) {
            uart_puts("[BOOT] config invalid (no backup)\r\n");
        } else if (cfg < 0) {
            uart_puts("[BOOT] config restore failed\r\n");
        }
    }

    uart_puts("[BOOT] wait 'R' 500ms\r\n");
    if (wait_update_req(UPDATE_HANDSHAKE_MS)) {
        do_update();                                /* 成功后复位，不返回 */
    }

    /* 有升级进度记录：升级包完整则继续/开始写入 App 区 */
    if (boot_ota_load(&rec)) {
        uart_puts("[BOOT] OTA log: state=");
        uart_put_dec(rec.state);
        uart_puts(" offset=");
        uart_put_hex32(rec.offset);
        uart_puts(" len=");
        uart_put_hex32(rec.len);
        uart_puts("\r\n");

        if (!ota_image_crc_ok(&rec)) {
            uart_puts("[BOOT] OTA image invalid, clear flag\r\n");
            (void)boot_flash_erase(BOOT_OTA_LOG_BASE, BOOT_OTA_LOG_SIZE);
        } else if (!ota_apply(&rec)) {
            uart_puts("[BOOT] apply failed, wait OTA\r\n");
            for (;;) {
                if (wait_update_req(1000U)) {
                    do_update();
                }
            }
        }
    }

    if (!image_valid(BOOT_APP_BASE)) {
        uart_puts("[BOOT] app invalid, wait OTA\r\n");
        for (;;) {
            if (wait_update_req(1000U)) {
                do_update();
            }
        }
    }

    uart_puts("[BOOT] boot app\r\n");
    uart_flush();
    delay_ms(10U);
    jump_to(BOOT_APP_BASE);
    return 0;
}
