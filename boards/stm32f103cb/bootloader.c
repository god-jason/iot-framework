/*
 * bootloader.c - STM32F1（128KB Flash）单 App 槽 Bootloader
 *
 * 分区（详见 partition.h）：
 *   Boot 8K / Boot Meta 4K / 配置 4K×2 / 错误日志 4K / App 52K / 升级包 52K
 *
 * 升级流程：
 *   1. 上位机经 UART1 发送新固件（按 App 区 0x08006000 链接），Bootloader
 *      把原始 bin 写入 ota 区，校验 CRC 后更新 boot_state（ota_valid=1、
 *      ota_len、ota_crc），随后复位；
 *   2. 下次启动读取 boot_state：ota_valid=1 则校验 ota 区 CRC，擦除 App 区，
 *      按 1KB 分块拷入 App 区；
 *   3. 每拷完一块追加一条升级历史（length=已写字节数）；掉电重启后从该
 *      偏移继续，保证 App 区最终是完整镜像（不会启动半写入的程序）；
 *   4. 全部写完并整段 CRC 校验通过后，擦除整个 Boot Meta 区并重写
 *      boot_state（ota_valid=0），再启动 App。
 *
 * 启动失败时 tries 累加，App 启动成功后清零；超过 5 次停留在 Bootloader
 * 等待串口 OTA。F103 无备份区，因此不做回滚。
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

/* 打印错误日志尾部（最多 1KB） */
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

    uart_puts("\r\n[BOOT] error log (tail):\r\n");
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

static bool ota_image_crc_ok(const boot_state_t *s)
{
    if (s->ota_len == 0U || s->ota_len > BOOT_OTA_SIZE) {
        return false;
    }
    return boot_crc32((const void *)(uintptr_t)BOOT_OTA_BASE, s->ota_len)
           == s->ota_crc;
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
 * 把 ota 区原始镜像写入 App 区。
 * 无历史：先写一条进度 0（起点），再擦除 App 区，从 0 开始；
 * 有历史：从最后进度继续，先擦除未确认尾部（掉电时最后一块可能半写）。
 * 每拷完 1KB 追加一条历史；全部完成并校验 CRC 后擦 Meta、重写 state。
 */
static bool ota_apply(boot_state_t *s)
{
    uint32_t off;
    uint32_t n;

    if (!boot_history_last_length(&off)) {
        uart_puts("[BOOT] erase app area\r\n");
        if (boot_history_append(s, 0U) != 0) {
            return false;
        }
        if (boot_flash_erase(BOOT_APP_BASE, BOOT_APP_SIZE) != 0) {
            return false;
        }
        off = 0U;
    } else if (off > s->ota_len) {
        (void)boot_state_save(s);       /* 状态异常：清历史，下次重来 */
        return false;
    } else if (off < BOOT_APP_SIZE) {
        uart_puts("[BOOT] erase unconfirmed tail\r\n");
        if (boot_flash_erase(BOOT_APP_BASE + off,
                             BOOT_APP_SIZE - off) != 0) {
            return false;
        }
    }

    uart_puts("[BOOT] writing app");
    while (off < s->ota_len) {
        n = s->ota_len - off;
        if (n > OTA_CHUNK_SIZE) {
            n = OTA_CHUNK_SIZE;
        }
        if (copy_chunk(off, n) != 0) {
            uart_puts("\r\n[BOOT] copy failed\r\n");
            return false;
        }
        off += n;
        if (boot_history_append(s, off) != 0) {
            uart_puts("\r\n[BOOT] history write failed\r\n");
            return false;
        }
        if ((off % (8U * 1024U)) == 0U) {
            uart_putc('.');
        }
    }
    uart_puts("\r\n");

    if (boot_crc32((const void *)(uintptr_t)BOOT_APP_BASE, s->ota_len) != s->ota_crc) {
        uart_puts("[BOOT] verify failed, restart next boot\r\n");
        (void)boot_state_save(s);       /* 清历史，保留 ota_valid */
        return false;
    }

    s->ota_valid = 0U;
    s->ota_len = 0U;
    s->ota_crc = 0U;
    s->tries = 0U;
    s->crash = 0U;
    if (boot_state_save(s) != 0) {
        return false;
    }
    uart_puts("[BOOT] upgrade complete\r\n");
    return true;
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

static void do_update(boot_state_t *s)
{
    uint32_t len = 0U;
    uint32_t crc = 0U;
    uint32_t got = 0U;
    uint32_t calc = 0xFFFFFFFFU;
    uint8_t buf[64];
    uint32_t n = 0U;
    uint32_t next_dot = 4096U;

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

    /* 原始 bin 已写入 ota 区：更新启动状态（升级标识 + 长度 + CRC） */
    s->ota_valid = 1U;
    s->ota_len = len;
    s->ota_crc = crc;
    s->tries = 0U;
    s->crash = 0U;
    s->reserved = 0U;
    if (boot_state_save(s) != 0) {
        uart_puts("[BOOT] OTA error: state save\r\n");
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
    boot_state_t st;

    (void)SysTick_Config(8000U);                    /* 8MHz / 1000 = 1ms */
    uart_init();

    boot_state_load(&st);

    uart_puts("\r\n[BOOT] f103cb single-slot OTA\r\n");
    uart_puts("[BOOT] tries=");
    uart_put_dec(st.tries);
    uart_puts(" crash=");
    uart_put_dec(st.crash);
    uart_puts(" ota=");
    uart_putc((st.ota_valid != 0U) ? 'Y' : 'N');
    uart_puts("\r\n");

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
        do_update(&st);                             /* 成功后复位，不返回 */
    }

    /* ota 标识：校验 ota 区并拷贝到 App 区 */
    if (st.ota_valid != 0U) {
        if (!ota_image_crc_ok(&st)) {
            uart_puts("[BOOT] OTA image invalid, clear flag\r\n");
            st.ota_valid = 0U;
            st.ota_len = 0U;
            st.ota_crc = 0U;
            (void)boot_state_save(&st);
        } else {
            uart_puts("[BOOT] apply OTA\r\n");
            if (!ota_apply(&st)) {
                uart_puts("[BOOT] apply failed, wait OTA\r\n");
                for (;;) {
                    if (wait_update_req(1000U)) {
                        do_update(&st);
                    }
                }
            }
        }
    }

    /* 启动失败超过上限：停在 Bootloader 等待串口 OTA */
    if (st.tries > BOOT_MAX_TRIES) {
        uart_puts("[BOOT] boot failures > ");
        uart_put_dec(BOOT_MAX_TRIES);
        uart_puts(", wait OTA\r\n");
        for (;;) {
            if (wait_update_req(1000U)) {
                do_update(&st);
            }
        }
    }

    if (st.tries < 0xFFFFFFFFU) {
        st.tries++;
    }
    (void)boot_state_save(&st);

    if (!image_valid(BOOT_APP_BASE)) {
        uart_puts("[BOOT] app invalid, wait OTA\r\n");
        for (;;) {
            if (wait_update_req(1000U)) {
                do_update(&st);
            }
        }
    }

    uart_puts("[BOOT] boot app\r\n");
    uart_flush();
    delay_ms(10U);
    jump_to(BOOT_APP_BASE);
    return 0;
}
