/*
 * bootloader.c - STM32F407VE（512KB Flash）A/B/C OTA Bootloader
 *
 * 分区（详见 partition.h）：
 *   S0 Bootloader / S1 Boot Meta / S2-S3 配置 /
 *   S4 系统日志 / S5 App 主程序 / S6 App 备份 / S7 App 升级包
 *
 * 启动流程：
 *   1. 打印系统日志尾部；
 *   2. C 区有有效升级包时：A -> B 备份，C -> A，擦除 C，进入试运行；
 *   3. 试运行期崩溃次数超限或启动次数用尽：B -> A 回滚，运行旧程序；
 *   4. A 区校验通过后跳转，App 初始化完成调用 boot_confirm() 确认。
 *
 * 串口 OTA 协议与 F1 版一致（ACK=0x06 / NAK=0x15），但镜像写入 C 区：
 * 先接收负载并校验 CRC，最后写 16B 头部，保证头部有效即整包完整。
 *
 * 不依赖 HAL / RTOS / libc，限制在 Flash 首个 16KB 扇区。
 */

#include "boot_meta.h"

#define UART_BRR_16MHZ_115200   139U
#define UPDATE_BYTE_TIMEOUT_MS  3000U
#define UPDATE_HANDSHAKE_MS     500U
#define BOOT_ACK_CHAR           0x06U
#define BOOT_NAK_CHAR           0x15U
#define COPY_CHUNK              256U
#define BOOT_SRAM_END           0x20020000U

uint32_t SystemCoreClock = 16000000U;

static volatile uint32_t s_ms;
static uint8_t s_copy_buf[COPY_CHUNK];

/* HSI 16MHz，无需 PLL */
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
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9/PA10 -> AF7 */
    GPIOA->MODER &= ~((3UL << 18) | (3UL << 20));
    GPIOA->MODER |= (2UL << 18) | (2UL << 20);
    GPIOA->AFR[1] &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA->AFR[1] |= (7UL << 4) | (7UL << 8);
    GPIOA->PUPDR &= ~(3UL << 20);
    GPIOA->PUPDR |= (1UL << 20);                  /* PA10 上拉 */

    USART1->BRR = UART_BRR_16MHZ_115200;
    USART1->CR1 = USART_CR1_RE | USART_CR1_TE | USART_CR1_UE;
}

static int uart_getc(uint32_t timeout_ms)
{
    uint32_t start = s_ms;

    for (;;) {
        if ((USART1->SR & USART_SR_RXNE) != 0U) {
            return (int)(USART1->DR & 0xFFU);
        }
        if (timeout_ms != 0U && (s_ms - start) >= timeout_ms) {
            return -1;
        }
    }
}

static void uart_putc(uint8_t c)
{
    while ((USART1->SR & USART_SR_TXE) == 0U) {
    }
    USART1->DR = c;
}

static void uart_flush(void)
{
    while ((USART1->SR & USART_SR_TC) == 0U) {
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

/* 打印系统日志尾部（最多 1KB，避免 64KB 全文打印拖慢启动） */
static void print_crash_log(void)
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

/* 向量表合法性：SP 在 SRAM 内、复位向量落在本槽范围内 */
static bool image_valid(uint32_t base)
{
    uint32_t sp = *(volatile uint32_t *)(uintptr_t)base;
    uint32_t pc = *(volatile uint32_t *)(uintptr_t)(base + 4U);

    if (sp < 0x20000000U || sp > BOOT_SRAM_END) {
        return false;
    }
    return (pc >= base) && (pc < (base + BOOT_SLOT_SIZE));
}

static bool slots_equal(uint32_t a, uint32_t b)
{
    return boot_crc32((const void *)(uintptr_t)a, BOOT_SLOT_SIZE) ==
           boot_crc32((const void *)(uintptr_t)b, BOOT_SLOT_SIZE);
}

/* 擦除 dst 覆盖的扇区后，把 src 的 len 字节复制过去 */
static int copy_region(uint32_t dst, uint32_t src, uint32_t len)
{
    uint32_t off = 0U;

    if (boot_flash_erase(dst, len) != 0) {
        return -1;
    }
    while (off < len) {
        uint32_t n = len - off;
        uint32_t i;

        if (n > COPY_CHUNK) {
            n = COPY_CHUNK;
        }
        for (i = 0U; i < n; i++) {
            s_copy_buf[i] = *(const volatile uint8_t *)(uintptr_t)(src + off + i);
        }
        if (boot_flash_write(dst + off, s_copy_buf, n) != 0) {
            return -1;
        }
        off += n;
    }
    return 0;
}

/* C 区升级包是否有效：头部 CRC + 负载 CRC 全通过才算有效 */
static bool ota_pkg_valid(const boot_ota_hdr_t **out)
{
    const boot_ota_hdr_t *h = (const boot_ota_hdr_t *)(uintptr_t)BOOT_SLOT_C_BASE;
    uint32_t max_len = BOOT_SLOT_SIZE - (uint32_t)sizeof(boot_ota_hdr_t);

    if (h->magic != BOOT_OTA_MAGIC ||
        h->hdr_crc != boot_crc32(h, 12U) ||
        h->len == 0U || h->len > max_len) {
        return false;
    }
    if (boot_crc32((const void *)(uintptr_t)(BOOT_SLOT_C_BASE +
                                             (uint32_t)sizeof(*h)), h->len)
        != h->crc) {
        return false;
    }
    *out = h;
    return true;
}

static bool backup_a_to_b(boot_meta_t *m)
{
    uart_puts("[BOOT] backup A -> B\r\n");
    if (copy_region(BOOT_SLOT_B_BASE, BOOT_SLOT_A_BASE, BOOT_SLOT_SIZE) != 0 ||
        !slots_equal(BOOT_SLOT_A_BASE, BOOT_SLOT_B_BASE)) {
        uart_puts("[BOOT] backup failed\r\n");
        return false;
    }
    m->b_valid = 1U;
    (void)boot_meta_save(m);
    return true;
}

static bool rollback_to_a(boot_meta_t *m)
{
    if (!m->b_valid) {
        uart_puts("[BOOT] rollback skipped: no backup\r\n");
        return false;
    }
    uart_puts("[BOOT] rollback B -> A\r\n");
    if (copy_region(BOOT_SLOT_A_BASE, BOOT_SLOT_B_BASE, BOOT_SLOT_SIZE) != 0 ||
        !slots_equal(BOOT_SLOT_A_BASE, BOOT_SLOT_B_BASE) ||
        !image_valid(BOOT_SLOT_A_BASE)) {
        uart_puts("[BOOT] rollback failed\r\n");
        return false;
    }
    m->state = BOOT_STATE_IDLE;
    m->tries = 0U;
    m->crash = 0U;
    (void)boot_meta_save(m);
    uart_puts("[BOOT] rollback done\r\n");
    return true;
}

/* 把 C 区升级包应用到 A 区；成功后擦除 C 并复位，不再返回 */
static bool apply_update(boot_meta_t *m)
{
    const boot_ota_hdr_t *h;
    uint32_t payload = BOOT_SLOT_C_BASE + (uint32_t)sizeof(boot_ota_hdr_t);
    bool applied;

    if (!ota_pkg_valid(&h)) {
        return false;
    }

    /* 上次拷贝可能已完成但未来得及擦除 C：避免重复备份/拷贝 */
    applied = (boot_crc32((const void *)(uintptr_t)BOOT_SLOT_A_BASE, h->len)
               == h->crc);

    if (applied) {
        uart_puts("[BOOT] package already applied\r\n");
    } else {
        /* A 区完好且尚未备份时才备份，防止把半写入的镜像备份到 B */
        if (!m->b_valid && image_valid(BOOT_SLOT_A_BASE)) {
            if (!backup_a_to_b(m)) {
                return false;
            }
        }
        uart_puts("[BOOT] apply C -> A\r\n");
        if (copy_region(BOOT_SLOT_A_BASE, payload, h->len) != 0 ||
            boot_crc32((const void *)(uintptr_t)BOOT_SLOT_A_BASE, h->len)
                != h->crc ||
            !image_valid(BOOT_SLOT_A_BASE)) {
            uart_puts("[BOOT] apply failed\r\n");
            return false;
        }
    }

    /* 先落试运行状态，再擦 C：中途掉电也不会丢掉“需要回滚”的信息 */
    m->state = BOOT_STATE_TRYING;
    m->tries = BOOT_MAX_TRIES;
    m->crash = 0U;
    (void)boot_meta_save(m);

    uart_puts("[BOOT] erase OTA area C\r\n");
    (void)boot_flash_erase(BOOT_SLOT_C_BASE, BOOT_SLOT_SIZE);

    uart_puts("[BOOT] update applied, rebooting\r\n");
    uart_flush();
    delay_ms(10U);
    NVIC_SystemReset();
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

/* ---------------------------- 串口升级（写入 C 区） ---------------------------- */

static void do_update(void)
{
    uint32_t len = 0U;
    uint32_t crc = 0U;
    uint32_t got = 0U;
    uint32_t calc = 0xFFFFFFFFU;
    uint32_t limit = BOOT_SLOT_SIZE - (uint32_t)sizeof(boot_ota_hdr_t);
    uint32_t hdr_size = (uint32_t)sizeof(boot_ota_hdr_t);
    boot_ota_hdr_t h;
    uint8_t buf[64];
    uint32_t n = 0U;
    uint32_t next_dot = 4096U;

    uart_puts("[BOOT] OTA request, target area C\r\n");
    uart_putc(BOOT_ACK_CHAR);

    if (uart_read_u32(&len) != 0 || uart_read_u32(&crc) != 0 ||
        len == 0U || len > limit) {
        uart_puts("[BOOT] OTA error: bad header\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }
    uart_puts("[BOOT] OTA len=");
    uart_put_hex32(len);
    uart_puts(" crc=");
    uart_put_hex32(crc);
    uart_puts("\r\n");

    if (boot_flash_erase(BOOT_SLOT_C_BASE, BOOT_SLOT_SIZE) != 0) {
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
            if (boot_flash_write(BOOT_SLOT_C_BASE + hdr_size + got - n,
                                 buf, n) != 0) {
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

    /* 头部最后写入：magic 有效即代表整包完整 */
    h.magic = BOOT_OTA_MAGIC;
    h.len = len;
    h.crc = crc;
    h.hdr_crc = boot_crc32(&h, 12U);
    if (boot_flash_write(BOOT_SLOT_C_BASE, &h, hdr_size) != 0) {
        uart_puts("[BOOT] OTA error: header write\r\n");
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
    boot_meta_t m;
    const boot_ota_hdr_t *pkg;

    (void)SysTick_Config(16000U);                    /* 16MHz / 1000 = 1ms */
    uart_init();
    boot_meta_load(&m);

    uart_puts("\r\n[BOOT] state=");
    uart_puts((m.state == BOOT_STATE_TRYING) ? "trial" : "idle");
    uart_puts(" tries=");
    uart_put_dec(m.tries);
    uart_puts(" crash=");
    uart_put_dec(m.crash);
    uart_puts(" backup=");
    uart_putc((m.b_valid != 0U) ? 'Y' : 'N');
    uart_puts("\r\n");

    print_crash_log();

    /* 试运行期崩溃次数超限：恢复旧程序 */
    if (m.state == BOOT_STATE_TRYING && m.crash >= BOOT_MAX_CRASH) {
        uart_puts("[BOOT] trial crashed ");
        uart_put_dec(m.crash);
        uart_puts(" times, restore old app\r\n");
        (void)rollback_to_a(&m);
    }

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
        do_update();
    }

    /* C 区有有效升级包：备份 A 到 B，升级 C 到 A */
    if (ota_pkg_valid(&pkg)) {
        uart_puts("[BOOT] found OTA package in C, len=");
        uart_put_hex32(pkg->len);
        uart_puts("\r\n");
        (void)apply_update(&m);      /* 成功后复位；失败则继续用旧 A 启动 */
    }

    /* 试运行计数：每次启动扣一次，App 确认后清零 */
    if (m.state == BOOT_STATE_TRYING) {
        if (m.tries == 0U) {
            uart_puts("[BOOT] trial attempts exhausted\r\n");
            (void)rollback_to_a(&m);
        } else {
            m.tries--;
            (void)boot_meta_save(&m);
            uart_puts("[BOOT] trial boot, left=");
            uart_put_dec(m.tries);
            uart_puts("\r\n");
        }
    }

    if (!image_valid(BOOT_SLOT_A_BASE)) {
        uart_puts("[BOOT] slot A invalid\r\n");
        if (m.b_valid && rollback_to_a(&m)) {
            uart_puts("[BOOT] fallback to backup\r\n");
        } else {
            uart_puts("[BOOT] no valid app, wait OTA\r\n");
            for (;;) {
                if (wait_update_req(1000U)) {
                    do_update();
                }
            }
        }
    }

    uart_puts("[BOOT] boot A\r\n");
    uart_flush();
    delay_ms(10U);
    jump_to(BOOT_SLOT_A_BASE);
    return 0;
}
