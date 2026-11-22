/*
 * bootloader.c - STM32F407VE（512KB Flash）APP/BACKUP/OTA Bootloader
 *
 * 分区（详见 partition.h）：
 *   S0 Bootloader / S1 Boot Meta（启动状态 + 升级历史）/ S2-S3 配置 /
 *   S4 错误日志 / S5 APP 主程序 / S6 BACKUP 备份 / S7 OTA 升级包
 *
 * 启动流程：
 *   1. 打印错误日志尾部；
 *   2. boot_state.ota_valid=1 时：APP -> BACKUP 备份，OTA -> APP，
 *      校验通过后清除标识；
 *   3. tries/crash 超过 5 次：用 BACKUP 恢复 APP，运行旧程序；
 *   4. APP 校验通过后跳转；App 初始化完成调用 boot_confirm() 清零计数。
 *
 * 串口 OTA 协议与 F1 版一致（ACK=0x06 / NAK=0x15），镜像写入 OTA 区：
 * OTA 区保存原始编译结果 bin（无包头），接收完成校验 CRC 后把长度/CRC/标识
 * 写入 boot_state；下次启动再备份 APP 到 BACKUP 并拷贝 OTA 到 APP。
 *
 * 不依赖 HAL / RTOS / libc，限制在 Flash 首个 16KB 扇区。
 */

#include "boot_meta.h"

#define UART_BRR_16MHZ_115200   139U
#define UPDATE_BYTE_TIMEOUT_MS  3000U
#define UPDATE_HANDSHAKE_MS     500U
#define BOOT_ACK_CHAR           0x06U
#define BOOT_NAK_CHAR           0x15U
#define COPY_CHUNK              1024U
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

/* 打印错误日志尾部（最多 1KB，避免 64KB 全文打印拖慢启动） */
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

/* ota 区（原始 bin）是否完整：长度 + 整段 CRC */
static bool ota_image_crc_ok(const boot_state_t *s)
{
    if (s->ota_len == 0U || s->ota_len > BOOT_SLOT_SIZE) {
        return false;
    }
    return boot_crc32((const void *)(uintptr_t)BOOT_SLOT_OTA_BASE, s->ota_len)
           == s->ota_crc;
}

/* 升级前备份当前 App 到 B；A 无效时清掉备份标志 */
static bool backup_a_to_b(boot_state_t *s)
{
    if (!image_valid(BOOT_SLOT_APP_BASE)) {
        s->reserved &= ~BOOT_STATE_FLAG_BACKUP;
        return true;
    }
    uart_puts("[BOOT] backup APP -> BACKUP\r\n");
    if (copy_region(BOOT_SLOT_BACKUP_BASE, BOOT_SLOT_APP_BASE, BOOT_SLOT_SIZE) != 0 ||
        !slots_equal(BOOT_SLOT_APP_BASE, BOOT_SLOT_BACKUP_BASE)) {
        uart_puts("[BOOT] backup failed\r\n");
        return false;
    }
    s->reserved |= BOOT_STATE_FLAG_BACKUP;
    return true;
}

/* 用备份区恢复 App 区 */
static bool restore_backup(boot_state_t *s)
{
    if ((s->reserved & BOOT_STATE_FLAG_BACKUP) == 0U) {
        uart_puts("[BOOT] no backup to restore\r\n");
        return false;
    }
    uart_puts("[BOOT] restore BACKUP -> APP\r\n");
    if (copy_region(BOOT_SLOT_APP_BASE, BOOT_SLOT_BACKUP_BASE, BOOT_SLOT_SIZE) != 0 ||
        !slots_equal(BOOT_SLOT_APP_BASE, BOOT_SLOT_BACKUP_BASE) ||
        !image_valid(BOOT_SLOT_APP_BASE)) {
        uart_puts("[BOOT] restore failed\r\n");
        return false;
    }
    uart_puts("[BOOT] restore done\r\n");
    return true;
}

/*
 * 把 OTA 区原始镜像写入 APP 区。
 * 无历史：先备份 APP -> BACKUP，落备份标志，写一条进度 0，再擦除 APP 区；
 * 有历史：APP 区是单个 128KB 扇区，无法只擦未确认尾部，直接整区重来。
 * 每拷完 1KB 追加一条历史；完成后校验 CRC，成功则清 ota 标识并重写 state。
 */
static bool ota_apply(boot_state_t *s)
{
    uint32_t off;
    uint32_t n;
    uint32_t i;

    if (!boot_history_last_length(&off)) {
        if (!backup_a_to_b(s)) {
            return false;
        }
        if (boot_state_save(s) != 0) {
            return false;
        }
        if (boot_history_append(s, 0U) != 0) {
            return false;
        }
        uart_puts("[BOOT] erase app area\r\n");
        if (boot_flash_erase(BOOT_SLOT_APP_BASE, BOOT_SLOT_SIZE) != 0) {
            return false;
        }
        off = 0U;
    } else if (off > s->ota_len) {
        (void)boot_state_save(s);       /* 状态异常：清历史，下次重来 */
        return false;
    } else if (off < s->ota_len) {
        uart_puts("[BOOT] resume from start (single sector)\r\n");
        if (boot_flash_erase(BOOT_SLOT_APP_BASE, BOOT_SLOT_SIZE) != 0) {
            return false;
        }
        if (boot_state_save(s) != 0) {
            return false;
        }
        if (boot_history_append(s, 0U) != 0) {
            return false;
        }
        off = 0U;
    }

    uart_puts("[BOOT] writing app");
    while (off < s->ota_len) {
        n = s->ota_len - off;
        if (n > COPY_CHUNK) {
            n = COPY_CHUNK;
        }
        for (i = 0U; i < n; i++) {
            s_copy_buf[i] = *(const volatile uint8_t *)(uintptr_t)(BOOT_SLOT_OTA_BASE + off + i);
        }
        if (boot_flash_write(BOOT_SLOT_APP_BASE + off, s_copy_buf, n) != 0) {
            uart_puts("\r\n[BOOT] copy failed\r\n");
            return false;
        }
        off += n;
        if (boot_history_append(s, off) != 0) {
            uart_puts("\r\n[BOOT] history write failed\r\n");
            return false;
        }
        if ((off % (16U * 1024U)) == 0U) {
            uart_putc('.');
        }
    }
    uart_puts("\r\n");

    if (boot_crc32((const void *)(uintptr_t)BOOT_SLOT_APP_BASE, s->ota_len)
        != s->ota_crc) {
        uart_puts("[BOOT] verify failed, restart next boot\r\n");
        (void)boot_state_save(s);       /* 清历史，保留 ota 标识 */
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

/* ---------------------------- 串口升级（写入 OTA 区） ---------------------------- */

static void do_update(boot_state_t *s)
{
    uint32_t len = 0U;
    uint32_t crc = 0U;
    uint32_t got = 0U;
    uint32_t calc = 0xFFFFFFFFU;
    uint8_t buf[64];
    uint32_t n = 0U;
    uint32_t next_dot = 4096U;

    uart_puts("[BOOT] OTA request, target area C\r\n");
    uart_putc(BOOT_ACK_CHAR);

    if (uart_read_u32(&len) != 0 || uart_read_u32(&crc) != 0 ||
        len == 0U || len > BOOT_SLOT_SIZE) {
        uart_puts("[BOOT] OTA error: bad header\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }
    uart_puts("[BOOT] OTA len=");
    uart_put_hex32(len);
    uart_puts(" crc=");
    uart_put_hex32(crc);
    uart_puts("\r\n");

    if (boot_flash_erase(BOOT_SLOT_OTA_BASE, BOOT_SLOT_SIZE) != 0) {
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
            if (boot_flash_write(BOOT_SLOT_OTA_BASE + got - n, buf, n) != 0) {
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

    (void)SysTick_Config(16000U);                    /* 16MHz / 1000 = 1ms */
    uart_init();
    boot_state_load(&st);

    uart_puts("\r\n[BOOT] f407ve APP/BACKUP/OTA\r\n");
    uart_puts("[BOOT] tries=");
    uart_put_dec(st.tries);
    uart_puts(" crash=");
    uart_put_dec(st.crash);
    uart_puts(" ota=");
    uart_putc((st.ota_valid != 0U) ? 'Y' : 'N');
    uart_puts(" backup=");
    uart_putc(((st.reserved & BOOT_STATE_FLAG_BACKUP) != 0U) ? 'Y' : 'N');
    uart_puts("\r\n");

    print_crash_log();

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
        do_update(&st);
    }

    /* ota 标识：校验 OTA 区并写入 APP 区（先备份 APP 到 BACKUP） */
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

    /* 启动失败/崩溃超过上限：恢复备份区程序 */
    if (st.tries > BOOT_MAX_TRIES || st.crash > BOOT_MAX_CRASH) {
        uart_puts("[BOOT] too many failures, restore backup\r\n");
        (void)restore_backup(&st);
        st.tries = 0U;
        st.crash = 0U;
        (void)boot_state_save(&st);
    }

    if (st.tries < 0xFFFFFFFFU) {
        st.tries++;
    }
    (void)boot_state_save(&st);

    if (!image_valid(BOOT_SLOT_APP_BASE)) {
        uart_puts("[BOOT] app invalid\r\n");
        if (restore_backup(&st)) {
            st.tries = 0U;
            st.crash = 0U;
            (void)boot_state_save(&st);
        } else {
            uart_puts("[BOOT] wait OTA\r\n");
            for (;;) {
                if (wait_update_req(1000U)) {
                    do_update(&st);
                }
            }
        }
    }

    uart_puts("[BOOT] boot APP\r\n");
    uart_flush();
    delay_ms(10U);
    jump_to(BOOT_SLOT_APP_BASE);
    return 0;
}
