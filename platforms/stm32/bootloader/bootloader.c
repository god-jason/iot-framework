/*
 * bootloader.c - STM32 串口升级 Bootloader（F1/F4 共用）
 *
 * 分区与升级参数见 platforms/stm32<family> 或 apps/<board> 中的 partition.h：
 *   - F103：单 App 槽，Boot 8K / KV 8K×2 / App 52K / 升级包 52K（无文本日志）；
 *   - F407：单 App 槽，Boot 16K / KV 16K×2 / 错误日志 16K /
 *            App 192K / 升级包 256K。
 *
 * 启动状态只保存 tries/crash（KV 区，src/fkv）；OTA 信息不落盘：
 * OTA 区自带包头（src/fota/fota.h），Bootloader 每次启动直接检测
 * 包头的魔术字/头 CRC/长度，有效则升级。升级成功后擦除 OTA 区；升级中断
 * （掉电/复位）时包头仍在，下次启动重新升级——不需要进度/续传。
 *
 * 串口 OTA 协议（两板一致）：
 *   1. 上位机经 UART1 发送 'R' 进入升级，随后发送 len(4B) + crc(4B)；
 *      len 是整个升级包（包头 + 负载）的长度，crc 覆盖这 len 字节；
 *   2. Bootloader 先接收 32B 包头并校验，再擦除 OTA 区，回 ACK；
 *   3. 上位机发送包头之后的负载，Bootloader 逐块写入 OTA 区；
 *   4. 校验整包 CRC 后复位；下次启动检测包头并升级。
 *
 * 不依赖 HAL / RTOS / libc，限制在 Flash 首个扇区。
 */

#include "boot_meta.h"
#include "fota.h"

#include "board_flash.h"
#include "crc32.h"

#if defined(FLASH_OTA_COMPRESS)
#include "deflate.h"
#endif

#include <stddef.h>

/* 芯片头与启动时钟：Bootloader 使用 HSI，不配置 PLL */
#if defined(STM32F10X_MD)
#include "stm32f10x.h"
#define BOOT_SYSCLK_HZ      8000000U
#define BOOT_UART_BRR       69U         /* 8MHz / 115200 ≈ 69.44 */
#elif defined(STM32F4xx) || defined(STM32F407xx)
#include "stm32f4xx.h"
#define BOOT_SYSCLK_HZ      16000000U
#define BOOT_UART_BRR       139U        /* 16MHz / 115200 ≈ 138.9 */
#endif

/* USART1 寄存器位（两系列布局一致，避免设备头宏差异） */
#define BOOT_USART_SR_RXNE  (1UL << 5)
#define BOOT_USART_SR_TC    (1UL << 6)
#define BOOT_USART_SR_TXE   (1UL << 7)
#define BOOT_USART_CR1_RE   (1UL << 2)
#define BOOT_USART_CR1_TE   (1UL << 3)
#define BOOT_USART_CR1_UE   (1UL << 13)

#define UPDATE_BYTE_TIMEOUT_MS  3000U
#define UPDATE_HANDSHAKE_MS     500U
#define BOOT_ACK_CHAR           0x06U
#define BOOT_NAK_CHAR           0x15U
#define COPY_CHUNK              1024U

#if defined(FLASH_LOG_BASE) && (FLASH_LOG_SIZE > 0U)
#define BOOT_HAS_LOG    1
#endif

uint32_t SystemCoreClock = BOOT_SYSCLK_HZ;

static volatile uint32_t s_ms;
static uint8_t s_copy_buf[COPY_CHUNK];

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
#if defined(STM32F10X_MD)
    RCC->APB2ENR |= (1UL << 2) | (1UL << 14);       /* IOPAEN | USART1EN */

    /* PA9 -> 复用推挽 50MHz，PA10 -> 上拉输入 */
    GPIOA->CRH &= ~(0x0FUL << 4);
    GPIOA->CRH |= (0x0BUL << 4);
    GPIOA->CRH &= ~(0x0FUL << 8);
    GPIOA->CRH |= (0x08UL << 8);
    GPIOA->ODR |= (1UL << 10);
#else
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9/PA10 -> AF7 */
    GPIOA->MODER &= ~((3UL << 18) | (3UL << 20));
    GPIOA->MODER |= (2UL << 18) | (2UL << 20);
    GPIOA->AFR[1] &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA->AFR[1] |= (7UL << 4) | (7UL << 8);
    GPIOA->PUPDR &= ~(3UL << 20);
    GPIOA->PUPDR |= (1UL << 20);                    /* PA10 上拉 */
#endif

    USART1->BRR = BOOT_UART_BRR;
    USART1->CR1 = BOOT_USART_CR1_RE | BOOT_USART_CR1_TE | BOOT_USART_CR1_UE;
}

static int uart_getc(uint32_t timeout_ms)
{
    uint32_t start = s_ms;

    for (;;) {
        if ((USART1->SR & BOOT_USART_SR_RXNE) != 0U) {
            return (int)(USART1->DR & 0xFFU);
        }
        if (timeout_ms != 0U && (s_ms - start) >= timeout_ms) {
            return -1;
        }
    }
}

static void uart_putc(uint8_t c)
{
    while ((USART1->SR & BOOT_USART_SR_TXE) == 0U) {
    }
    USART1->DR = c;
}

static void uart_flush(void)
{
    while ((USART1->SR & BOOT_USART_SR_TC) == 0U) {
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

/* 打印错误日志尾部（最多 2KB，避免大分区全文打印拖慢启动） */
#ifdef BOOT_HAS_LOG
static void print_error_log(void)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)FLASH_LOG_BASE;
    uint32_t end = 0U;
    uint32_t start;
    uint32_t i;

    while (end < FLASH_LOG_SIZE && p[end] != 0xFFU) {
        end++;
    }
    if (end == 0U) {
        return;
    }
    start = (end > 2048U) ? (end - 2048U) : 0U;
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
#endif /* BOOT_HAS_LOG */

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

    if (sp < 0x20000000U || sp > FLASH_SRAM_END) {
        return false;
    }
    return (pc >= base) && (pc < (base + FLASH_APP_SIZE));
}

/* 读取 OTA 区包头并校验（结构 + 板级范围），无有效包头返回 false */
static bool ota_hdr_read(fota_hdr_t *h)
{
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)FLASH_OTA_BASE;
    uint8_t *d = (uint8_t *)h;
    uint32_t i;

    for (i = 0U; i < FOTA_HDR_SIZE; i++) {
        d[i] = p[i];
    }
    if (!fota_hdr_valid(h)) {
        return false;
    }
    return h->image_len <= FLASH_APP_SIZE &&
           h->data_len <= (FLASH_OTA_SIZE - FOTA_HDR_SIZE);
}

/* 校验 OTA 区负载完整（擦除 APP 之前先确认数据源可用） */
static bool ota_data_ok(const fota_hdr_t *h)
{
    return crc32((const void *)(uintptr_t)(FLASH_OTA_BASE + FOTA_HDR_SIZE),
                 h->data_len) == h->data_crc;
}

/* 升级成功：擦除 OTA 区，去掉包头魔术字 */
static void ota_clear(void)
{
#ifdef FLASH_OTA_ERASE_FULL
    (void)board_flash_erase(FLASH_OTA_BASE, FLASH_OTA_SIZE);
#else
    (void)board_flash_erase(FLASH_OTA_BASE, FLASH_OTA_HDR_ERASE);
#endif
}

/* 原始镜像：从包头之后整段拷入 APP 区 */
static bool app_copy_raw(const fota_hdr_t *h)
{
    uint32_t off = 0U;
    uint32_t n;
    uint32_t i;

    uart_puts("[BOOT] writing app");
    while (off < h->data_len) {
        n = h->data_len - off;
        if (n > COPY_CHUNK) {
            n = COPY_CHUNK;
        }
        for (i = 0U; i < n; i++) {
            s_copy_buf[i] = *(const volatile uint8_t *)(uintptr_t)
                            (FLASH_OTA_BASE + FOTA_HDR_SIZE + off + i);
        }
        if (board_flash_write(FLASH_APP_BASE + off, s_copy_buf, n) != 0) {
            uart_puts("\r\n[BOOT] copy failed\r\n");
            return false;
        }
        off += n;
        if ((off % FLASH_PROGRESS_STEP) == 0U) {
            uart_putc('.');
        }
    }
    uart_puts("\r\n");
    return true;
}

#if defined(FLASH_OTA_COMPRESS)
/* 压缩镜像：流式解压（1KB 一批）边解压边写 APP 区 */
static bool app_inflate(const fota_hdr_t *h)
{
    static deflate_inflate_t s_inflate;
    uint32_t off = 0U;
    int done = 0;

    if (deflate_inflate_begin(&s_inflate,
            (const void *)(uintptr_t)(FLASH_OTA_BASE + FOTA_HDR_SIZE),
            h->data_len) != DEFLATE_OK) {
        uart_puts("[BOOT] inflate init failed\r\n");
        return false;
    }

    uart_puts("[BOOT] inflate app");
    while (done == 0) {
        uint32_t produced = 0U;

        if (deflate_inflate_run(&s_inflate, s_copy_buf, sizeof(s_copy_buf),
                                &produced, &done) != DEFLATE_OK) {
            uart_puts("\r\n[BOOT] inflate failed\r\n");
            return false;
        }
        if (produced != 0U) {
            if ((off + produced) > h->image_len) {
                uart_puts("\r\n[BOOT] inflate overflow\r\n");
                return false;
            }
            if (board_flash_write(FLASH_APP_BASE + off, s_copy_buf,
                                  produced) != 0) {
                uart_puts("\r\n[BOOT] copy failed\r\n");
                return false;
            }
            off += produced;
            if ((off % FLASH_PROGRESS_STEP) < produced) {
                uart_putc('.');
            }
        }
    }
    uart_puts("\r\n");

    if (off != h->image_len) {
        uart_puts("[BOOT] inflate length mismatch\r\n");
        return false;
    }
    return true;
}
#endif /* FLASH_OTA_COMPRESS */

/*
 * 有有效包头时执行升级：
 *   1  升级完成（OTA 区已清空）
 *   0  OTA 区没有有效升级包，或负载校验失败（已忽略），继续正常启动
 *  -1  升级失败（APP 可能已被破坏），需停在 Bootloader 等待串口 OTA
 */
static int ota_apply(boot_state_t *s)
{
    fota_hdr_t h;

    if (!ota_hdr_read(&h)) {
        return 0;
    }

    uart_puts("[BOOT] OTA found: image=");
    uart_put_dec(h.image_len);
    uart_puts(" data=");
    uart_put_dec(h.data_len);
    uart_puts(" flags=");
    uart_put_dec(h.flags);
    uart_puts("\r\n");

    if (!ota_data_ok(&h)) {
        uart_puts("[BOOT] OTA data crc mismatch, ignore\r\n");
        ota_clear();
        return 0;
    }

#ifndef FLASH_OTA_COMPRESS
    if ((h.flags & FOTA_FLAG_DEFLATE) != 0U) {
        uart_puts("[BOOT] compressed OTA not supported\r\n");
        return -1;
    }
#endif

    uart_puts("[BOOT] erase app area\r\n");
    if (board_flash_erase(FLASH_APP_BASE, h.image_len) != 0) {
        uart_puts("[BOOT] erase app failed\r\n");
        return -1;
    }

#if defined(FLASH_OTA_COMPRESS)
    if ((h.flags & FOTA_FLAG_DEFLATE) != 0U) {
        if (!app_inflate(&h)) {
            return -1;
        }
    } else
#endif
    {
        if (!app_copy_raw(&h)) {
            return -1;
        }
    }

    if (crc32((const void *)(uintptr_t)FLASH_APP_BASE, h.image_len)
        != h.image_crc) {
        uart_puts("[BOOT] verify failed, wait OTA\r\n");
        return -1;
    }

    ota_clear();
    s->tries = 0U;
    s->crash = 0U;
    (void)boot_state_save(s);
    uart_puts("[BOOT] upgrade complete\r\n");
    return 1;
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

static void do_update(void)
{
    uint32_t len = 0U;
    uint32_t crc = 0U;
    uint32_t got = 0U;
    uint32_t calc = 0xFFFFFFFFU;
    uint8_t buf[64];
    fota_hdr_t hdr;
    uint8_t *hp = (uint8_t *)&hdr;
    uint32_t n = 0U;
    uint32_t i;
    uint32_t next_dot = 4096U;

    uart_puts("[BOOT] OTA request, target area OTA\r\n");
    uart_putc(BOOT_ACK_CHAR);

    if (uart_read_u32(&len) != 0 || uart_read_u32(&crc) != 0 ||
        len <= FOTA_HDR_SIZE || len > FLASH_OTA_SIZE) {
        uart_puts("[BOOT] OTA error: bad header\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }
    uart_puts("[BOOT] OTA len=");
    uart_put_hex32(len);
    uart_puts(" crc=");
    uart_put_hex32(crc);
    uart_puts("\r\n");

    /* 先收包头并校验，再擦除，避免破坏 OTA 区中已有的升级包 */
    for (i = 0U; i < FOTA_HDR_SIZE; i++) {
        int c = uart_getc(UPDATE_BYTE_TIMEOUT_MS);

        if (c < 0) {
            uart_puts("[BOOT] OTA error: rx timeout\r\n");
            uart_putc(BOOT_NAK_CHAR);
            return;
        }
        hp[i] = (uint8_t)c;
    }
    if (!fota_hdr_valid(&hdr) || hdr.data_len != (len - FOTA_HDR_SIZE) ||
        hdr.image_len > FLASH_APP_SIZE) {
        uart_puts("[BOOT] OTA error: bad package header\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }

#ifdef FLASH_OTA_ERASE_FULL
    /* 扇区擦除：整块 OTA 区一起擦 */
    if (board_flash_erase(FLASH_OTA_BASE, FLASH_OTA_SIZE) != 0) {
#else
    /* 页擦除：只擦本次升级包覆盖的页 */
    if (board_flash_erase(FLASH_OTA_BASE, len) != 0) {
#endif
        uart_puts("[BOOT] OTA error: erase failed\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }
    if (board_flash_write(FLASH_OTA_BASE, &hdr, FOTA_HDR_SIZE) != 0) {
        uart_puts("[BOOT] OTA error: flash write\r\n");
        uart_putc(BOOT_NAK_CHAR);
        return;
    }
    calc = crc32_update(calc, &hdr, FOTA_HDR_SIZE);
    got = FOTA_HDR_SIZE;

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
            calc = crc32_update(calc, buf, n);
            if (board_flash_write(FLASH_OTA_BASE + got - n, buf, n) != 0) {
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

    /* 升级包已写入 OTA 区：复位后由开机检测包头并升级 */
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
    int rc;

    (void)SysTick_Config(BOOT_SYSCLK_HZ / 1000U);
    uart_init();
    boot_state_load(&st);

    uart_puts("\r\n[BOOT] " FLASH_BANNER "\r\n");
    uart_puts("[BOOT] tries=");
    uart_put_dec(st.tries);
    uart_puts(" crash=");
    uart_put_dec(st.crash);
    uart_puts("\r\n");

#ifdef BOOT_HAS_LOG
    print_error_log();
#endif

    uart_puts("[BOOT] wait 'R' 500ms\r\n");
    if (wait_update_req(UPDATE_HANDSHAKE_MS)) {
        do_update();                    /* 成功后复位，失败则继续启动流程 */
    }

    /*
     * 每次启动直接检测 OTA 区包头：有效则升级。
     * 升级中断（掉电/复位）时包头仍在，下次启动会重新升级。
     */
    rc = ota_apply(&st);
    if (rc < 0) {
        uart_puts("[BOOT] apply failed, wait OTA\r\n");
        for (;;) {
            if (wait_update_req(1000U)) {
                do_update();
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
                do_update();
            }
        }
    }

    if (st.tries < 0xFFFFFFFFU) {
        st.tries++;
    }
    (void)boot_state_save(&st);

    if (!image_valid(FLASH_APP_BASE)) {
        uart_puts("[BOOT] app invalid, wait OTA\r\n");
        for (;;) {
            if (wait_update_req(1000U)) {
                do_update();
            }
        }
    }

    uart_puts("[BOOT] boot APP\r\n");
    uart_flush();
    delay_ms(10U);
    jump_to(FLASH_APP_BASE);
    return 0;
}
