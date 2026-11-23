/*
 * boot_crash.c - STM32 错误日志与崩溃记录（F1/F4 共用）
 *
 * 芯片相关的 Flash 擦写原语位于 platforms/stm32<family>/boot_flash.c；
 * 日志区地址/大小与栈顶参数位于 platforms/stm32<family>/partition.h。
 */

#include "boot_crash.h"
#include "boot_meta.h"
#include "boot_flash.h"

#include <stddef.h>

/* SCB 故障寄存器：仅本文件需要，按芯片系列包含 CMSIS 设备头 */
#if defined(STM32F10X_MD)
#include "stm32f10x.h"
#elif defined(STM32F4xx) || defined(STM32F407xx)
#include "stm32f4xx.h"
#elif defined(STM32H7xx)
#include "stm32h7xx.h"
#endif

/* ==========================================================================
 * 错误日志（BOOT_LOG_BASE 起文本追加区，带 Unix 时间）
 *
 * 每条格式：[<unix>] <text>\r\n
 * 空间不足时整区擦除后从头部重新追加；掉电最多丢失正在写的一行。
 * ========================================================================== */

#define BOOT_LOG_STACK_WORDS    128U    /* 栈快照字数（512 字节） */
#define BOOT_LOG_LINE_SIZE      160U    /* 单行文本缓冲 */

static char s_log_line[BOOT_LOG_LINE_SIZE];   /* 静态缓冲，异常上下文不占栈 */

static char *log_put_str(char *p, const char *s)
{
    while (*s != '\0') {
        *p++ = *s++;
    }
    return p;
}

/* 8 位十六进制（不带 0x 前缀，便于 crash_decode.py 直接解析 pc=/lr=） */
static char *log_put_hex(char *p, uint32_t v)
{
    int shift;

    for (shift = 28; shift >= 0; shift -= 4) {
        uint32_t d = (v >> (uint32_t)shift) & 0x0FU;

        *p++ = (char)(d < 10U ? ('0' + d) : ('A' + d - 10U));
    }
    return p;
}

static char *log_put_dec(char *p, uint32_t v)
{
    char tmp[10];
    uint32_t n = 0U;

    if (v == 0U) {
        *p++ = '0';
        return p;
    }
    while (v > 0U) {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (n > 0U) {
        *p++ = tmp[--n];
    }
    return p;
}

/* 第一条未写入（0xFF）的位置，即当前追加末尾 */
static uint32_t log_end(void)
{
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)BOOT_LOG_BASE;
    uint32_t i;

    for (i = 0U; i < BOOT_LOG_SIZE; i++) {
        if (p[i] == 0xFFU) {
            break;
        }
    }
    return i;
}

/* 追加一行 "[unix] text\r\n"；空间不足时整区擦除后从头部重写 */
void boot_log_append(uint32_t unix_time, const char *text)
{
    char line[BOOT_LOG_LINE_SIZE];
    char *p = line;
    uint32_t n;
    uint32_t off;

    p = log_put_str(p, "[");
    p = log_put_dec(p, unix_time);
    p = log_put_str(p, "] ");
    while (*text != '\0' && (uint32_t)(p - line) < (BOOT_LOG_LINE_SIZE - 4U)) {
        *p++ = *text++;
    }
    *p++ = '\r';
    *p++ = '\n';
    n = (uint32_t)(p - line);
#ifdef BOOT_LOG_PAD_EVEN
    /* Flash 按半字编程时长度需补齐为偶数（补 0x20，不影响文本解析） */
    if ((n & 1U) != 0U) {
        *p++ = ' ';
        n++;
    }
#endif

    off = log_end();
    if ((off + n) > BOOT_LOG_SIZE) {
        if (boot_flash_erase(BOOT_LOG_BASE, BOOT_LOG_SIZE) != 0) {
            return;
        }
        off = 0U;
    }
    (void)boot_flash_write(BOOT_LOG_BASE + off, line, n);
}

void boot_crash_log(const uint32_t *frame, uint32_t sp, uint32_t exc_return,
                          uint32_t unix_time)
{
    boot_state_t s;
    char *p;
    uint32_t addr;
    uint32_t words;
    uint32_t i;

    if (frame == NULL) {
        return;
    }

    /* crash 累计并写回状态（Bootloader 在 F4 上据此恢复备份区） */
    boot_state_load(&s);
    if (s.crash < 0xFFFFFFFFU) {
        s.crash++;
    }
    (void)boot_state_save(&s);

    /* 1) 摘要行 */
    p = s_log_line;
    p = log_put_str(p, "reason=HARDFAULT");
    p = log_put_str(p, BOOT_CRASH_TAG);
    p = log_put_str(p, " crash=");
    p = log_put_dec(p, s.crash);
    *p = '\0';
    boot_log_append(unix_time, s_log_line);

    /* 2) 异常栈帧：PC/LR/SP/xPSR */
    p = s_log_line;
    p = log_put_str(p, "pc=");
    p = log_put_hex(p, frame[6]);
    p = log_put_str(p, " lr=");
    p = log_put_hex(p, frame[5]);
    p = log_put_str(p, " sp=");
    p = log_put_hex(p, sp);
    p = log_put_str(p, " xpsr=");
    p = log_put_hex(p, frame[7]);
    *p = '\0';
    boot_log_append(unix_time, s_log_line);

    /* 3) 通用寄存器 */
    p = s_log_line;
    p = log_put_str(p, "r0=");
    p = log_put_hex(p, frame[0]);
    p = log_put_str(p, " r1=");
    p = log_put_hex(p, frame[1]);
    p = log_put_str(p, " r2=");
    p = log_put_hex(p, frame[2]);
    p = log_put_str(p, " r3=");
    p = log_put_hex(p, frame[3]);
    p = log_put_str(p, " r12=");
    p = log_put_hex(p, frame[4]);
    *p = '\0';
    boot_log_append(unix_time, s_log_line);

    /* 4) 故障状态寄存器 */
    p = s_log_line;
    p = log_put_str(p, "cfsr=");
    p = log_put_hex(p, SCB->CFSR);
    p = log_put_str(p, " hfsr=");
    p = log_put_hex(p, SCB->HFSR);
    p = log_put_str(p, " excr=");
    p = log_put_hex(p, exc_return);
    *p = '\0';
    boot_log_append(unix_time, s_log_line);

    p = s_log_line;
    p = log_put_str(p, "mmfar=");
    p = log_put_hex(p, SCB->MMFAR);
    p = log_put_str(p, " bfar=");
    p = log_put_hex(p, SCB->BFAR);
    *p = '\0';
    boot_log_append(unix_time, s_log_line);

    /* 5) 栈快照：从异常栈指针起 128 字，每行 8 字（crash_decode.py 可扫描） */
    addr = sp & ~3U;
    if (addr > BOOT_SRAM_END) {
        addr = BOOT_SRAM_END;
    }
    words = BOOT_LOG_STACK_WORDS;
    if ((BOOT_SRAM_END - addr) / 4U < words) {
        words = (BOOT_SRAM_END - addr) / 4U;
    }
    i = 0U;
    while (i < words) {
        uint32_t j;

        p = s_log_line;
        p = log_put_str(p, "stack[");
        p = log_put_hex(p, addr + i * 4U);
        p = log_put_str(p, "]:");
        for (j = 0U; j < 8U && i < words; j++, i++) {
            uint32_t v = *(const volatile uint32_t *)(uintptr_t)(addr + i * 4U);

            *p++ = ' ';
            p = log_put_hex(p, v);
        }
        *p = '\0';
        boot_log_append(unix_time, s_log_line);
    }
}
