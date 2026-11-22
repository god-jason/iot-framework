/*
 * boot_meta.c - STM32F103 Boot Meta（数据 meta + 升级日志）、CRC32、Flash 与错误日志
 *
 * 单 App 槽方案：升级包写入 OTA 区后，Bootloader 按进度日志分块拷入
 * App 区，掉电后从最后一条记录继续，避免半写入的 App 被启动。
 *
 * 该文件同时编入 Bootloader 与 App：
 *   - Bootloader 构建使用 -nostdlib，因此这里不调用任何 libc 函数；
 *   - App 只用到 boot_crash_log()，链接器会按
 *     -ffunction-sections --gc-sections 丢弃未使用的 Flash 函数。
 */

#include "boot_meta.h"

#include <stddef.h>

/* ==========================================================================
 * 一、CRC32（CRC-32/IEEE，多项式 0xEDB88320，无查表，节省 Flash）
 * ========================================================================== */

/*
 * 增量计算：crc 初值传 0xFFFFFFFF，最后再异或 0xFFFFFFFF。
 * 逐位实现只有几十字节代码，56KB 全量校验在 8MHz 下约 0.2s。
 */
uint32_t boot_crc32_update(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;

    while (len-- > 0U) {
        crc ^= (uint32_t)(*p++);
        for (i = 0U; i < 8U; i++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return crc;
}

/* 一次性计算 */
uint32_t boot_crc32(const void *data, uint32_t len)
{
    return boot_crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}

/* ==========================================================================
 * 二、Flash 驱动（F103 寄存器操作，擦除按页、编程按半字）
 * ========================================================================== */

/* Flash 寄存器位（避免与设备头宏重名） */
#define BOOT_FLASH_SR_BSY       (1UL << 0)
#define BOOT_FLASH_SR_PGERR     (1UL << 2)
#define BOOT_FLASH_SR_WRPRTERR  (1UL << 4)
#define BOOT_FLASH_CR_PG        (1UL << 0)
#define BOOT_FLASH_CR_PER       (1UL << 1)
#define BOOT_FLASH_CR_STRT      (1UL << 6)
#define BOOT_FLASH_CR_LOCK      (1UL << 7)

#define BOOT_FLASH_TIMEOUT      0x200000UL  /* Flash 忙等上限（约几十 ms） */

/* 等待 Flash 操作完成并检查错误标志；返回 0 成功 */
static int boot_flash_wait(void)
{
    uint32_t timeout = BOOT_FLASH_TIMEOUT;

    while ((FLASH->SR & BOOT_FLASH_SR_BSY) != 0U) {
        if (--timeout == 0U) {
            return -1;
        }
    }
    if ((FLASH->SR & (BOOT_FLASH_SR_PGERR | BOOT_FLASH_SR_WRPRTERR)) != 0U) {
        FLASH->SR = BOOT_FLASH_SR_PGERR | BOOT_FLASH_SR_WRPRTERR;  /* 写 1 清除 */
        return -1;
    }
    return 0;
}

/* 解锁 Flash（重复调用安全） */
static void boot_flash_unlock(void)
{
    if ((FLASH->CR & BOOT_FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
}

/* 上锁 Flash */
static void boot_flash_lock(void)
{
    FLASH->CR |= BOOT_FLASH_CR_LOCK;
}

/*
 * 擦除 addr 开始、覆盖 len 字节的 Flash 区域（按页向上取整）。
 * 约束：addr 必须在 A 区之后且按页对齐；不允许擦除 Bootloader 自身。
 */
int boot_flash_erase(uint32_t addr, uint32_t len)
{
    uint32_t page;
    uint32_t count;
    int rc = 0;

    if (len == 0U || addr < BOOT_META_BASE ||
        (addr % BOOT_FLASH_PAGE) != 0U) {
        return -1;
    }
    count = (len + BOOT_FLASH_PAGE - 1U) / BOOT_FLASH_PAGE;

    boot_flash_unlock();
    for (page = 0U; page < count; page++) {
        uint32_t p = addr + page * BOOT_FLASH_PAGE;

        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOOT_FLASH_CR_PER;      /* 页擦除 */
        FLASH->AR = p;                      /* 目标地址 */
        FLASH->CR |= BOOT_FLASH_CR_STRT;    /* 启动擦除 */
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = 0U;
    }
    boot_flash_lock();
    return rc;
}

/*
 * 从 addr 开始写入 len 字节（半字编程）。
 * 奇数长度时最后一个半字的高字节补 0xFF（Flash 擦除态），不参与 CRC。
 * 约束：addr 必须 2 字节对齐且在 A 区之后。
 */
int boot_flash_write(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;
    int rc = 0;

    if (data == NULL || len == 0U || addr < BOOT_META_BASE ||
        (addr & 1U) != 0U) {
        return -1;
    }

    boot_flash_unlock();
    for (i = 0U; i < len; i += 2U) {
        uint16_t hw = (uint16_t)p[i];

        if ((i + 1U) < len) {
            hw |= (uint16_t)((uint16_t)p[i + 1U] << 8);
        } else {
            hw |= 0xFF00U;
        }
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOOT_FLASH_CR_PG;       /* 半字编程 */
        *(volatile uint16_t *)(uintptr_t)(addr + i) = hw;
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = 0U;
    }
    boot_flash_lock();
    return rc;
}

/* ==========================================================================
 * 三、启动状态与升级历史（Boot Meta 区）
 * ========================================================================== */

static void state_copy(boot_state_t *dst, const boot_state_t *src)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0U; i < (uint32_t)sizeof(*dst); i++) {
        d[i] = s[i];
    }
}

bool boot_state_valid(const boot_state_t *s)
{
    if (s->magic != BOOT_STATE_MAGIC) {
        return false;
    }
    if (s->crc != boot_crc32(s, (uint32_t)sizeof(*s) - 4U)) {
        return false;
    }
    return s->ota_valid <= 1U;
}

void boot_state_load(boot_state_t *s)
{
    const boot_state_t *p = (const boot_state_t *)(uintptr_t)BOOT_META_BASE;

    if (s == NULL) {
        return;
    }
    if (boot_state_valid(p)) {
        state_copy(s, p);
        return;
    }
    s->magic = BOOT_STATE_MAGIC;
    s->tries = 0U;
    s->crash = 0U;
    s->ota_valid = 0U;
    s->ota_len = 0U;
    s->ota_crc = 0U;
    s->reserved = 0U;
    s->crc = 0U;
}

/* 擦除整个 Meta 区后写入状态；升级历史随之清空 */
int boot_state_save(boot_state_t *s)
{
    const uint8_t *src;
    const uint8_t *verify;
    uint32_t i;

    if (s == NULL) {
        return -1;
    }
    s->magic = BOOT_STATE_MAGIC;
    s->crc = boot_crc32(s, (uint32_t)sizeof(*s) - 4U);

    if (boot_flash_erase(BOOT_META_BASE, BOOT_META_SIZE) != 0) {
        return -1;
    }
    if (boot_flash_write(BOOT_META_BASE, s, (uint32_t)sizeof(*s)) != 0) {
        return -1;
    }

    src = (const uint8_t *)s;
    verify = (const uint8_t *)(uintptr_t)BOOT_META_BASE;
    for (i = 0U; i < (uint32_t)sizeof(*s); i++) {
        if (verify[i] != src[i]) {
            return -1;
        }
    }
    return 0;
}

static const boot_ota_history_t *history_at(uint32_t index)
{
    return (const boot_ota_history_t *)(uintptr_t)
           (BOOT_HISTORY_BASE + index * BOOT_HISTORY_SLOT);
}

static bool history_valid(const boot_ota_history_t *h)
{
    if (h->magic != BOOT_HISTORY_MAGIC) {
        return false;
    }
    return h->crc == boot_crc32(h, (uint32_t)sizeof(*h) - 4U);
}

/* 取最后一条有效历史记录的进度 */
bool boot_history_last_length(uint32_t *length)
{
    uint32_t i;

    if (length == NULL) {
        return false;
    }
    for (i = BOOT_HISTORY_COUNT; i > 0U; i--) {
        const boot_ota_history_t *h = history_at(i - 1U);

        if (history_valid(h)) {
            *length = h->length;
            return true;
        }
    }
    return false;
}

/*
 * 追加一条升级进度记录。
 * 历史区写满时先擦除整个 Meta 区并重写当前状态（保留 ota 标识），
 * 再写入这一条，保证升级仍可从当前进度继续。
 */
int boot_history_append(const boot_state_t *s, uint32_t length)
{
    boot_ota_history_t h;
    boot_state_t tmp;
    uint32_t addr = 0U;
    uint32_t i;

    h.magic = BOOT_HISTORY_MAGIC;
    h.length = length;
    h.reserved = 0U;
    h.crc = boot_crc32(&h, (uint32_t)sizeof(h) - 4U);

    for (i = 0U; i < BOOT_HISTORY_COUNT; i++) {
        if (history_at(i)->magic != BOOT_HISTORY_MAGIC) {
            addr = BOOT_HISTORY_BASE + i * BOOT_HISTORY_SLOT;
            break;
        }
    }
    if (addr == 0U) {
        state_copy(&tmp, s);
        if (boot_state_save(&tmp) != 0) {
            return -1;
        }
        addr = BOOT_HISTORY_BASE;
    }
    return boot_flash_write(addr, &h, (uint32_t)sizeof(h));
}

/* App 启动确认：tries/crash 清零 */
void boot_confirm(void)
{
    boot_state_t s;

    boot_state_load(&s);
    if (s.tries != 0U || s.crash != 0U) {
        s.tries = 0U;
        s.crash = 0U;
        (void)boot_state_save(&s);
    }
}

/* ==========================================================================
 * 五、错误日志（BOOT_LOG_BASE 起 4KB 文本，追加写入，带 Unix 时间）
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

/* 追加一行 "[unix] text\r\n"；长度补齐为偶数（半字编程），写满后整区擦除 */
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
    if ((n & 1U) != 0U) {
        *p++ = ' ';
        n++;
    }

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

    /* crash 累计并写回状态（Bootloader 在 407 上据此恢复备份区） */
    boot_state_load(&s);
    if (s.crash < 0xFFFFFFFFU) {
        s.crash++;
    }
    (void)boot_state_save(&s);

    /* 1) 摘要行 */
    p = s_log_line;
    p = log_put_str(p, "reason=HARDFAULT crash=");
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
    if (addr > 0x20005000U) {
        addr = 0x20005000U;
    }
    words = BOOT_LOG_STACK_WORDS;
    if ((0x20005000U - addr) / 4U < words) {
        words = (0x20005000U - addr) / 4U;
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

/* ==========================================================================
 * 六、配置区（主区 + 备份区，启动时校验并还原）
 * ========================================================================== */

bool boot_config_valid_at(uint32_t base)
{
    const boot_config_hdr_t *h = (const boot_config_hdr_t *)(uintptr_t)base;
    uint32_t max_len = BOOT_CONFIG_SIZE - (uint32_t)sizeof(*h);

    if (h->magic != BOOT_CONFIG_MAGIC || h->len > max_len) {
        return false;
    }
    return boot_crc32((const uint8_t *)(uintptr_t)base + sizeof(*h), h->len)
           == h->crc;
}

static int boot_config_write_area(uint32_t base, const void *data, uint32_t len)
{
    boot_config_hdr_t h;

    h.magic = BOOT_CONFIG_MAGIC;
    h.len = len;
    h.crc = boot_crc32(data, len);

    if (boot_flash_erase(base, BOOT_CONFIG_SIZE) != 0) {
        return -1;
    }
    if (boot_flash_write(base, &h, (uint32_t)sizeof(h)) != 0) {
        return -1;
    }
    if (len != 0U &&
        boot_flash_write(base + (uint32_t)sizeof(h), data, len) != 0) {
        return -1;
    }
    return boot_config_valid_at(base) ? 0 : -1;
}

int boot_config_save(const void *data, uint32_t len)
{
    if ((data == NULL && len != 0U) ||
        len > BOOT_CONFIG_SIZE - (uint32_t)sizeof(boot_config_hdr_t)) {
        return -1;
    }

    /* 先写备份区，再写主区：主区写失败时启动可从备份还原 */
    if (boot_config_write_area(BOOT_CONFIG_BASE1, data, len) != 0) {
        return -1;
    }
    if (boot_config_write_area(BOOT_CONFIG_BASE0, data, len) != 0) {
        return -1;
    }
    return 0;
}

int boot_config_restore(void)
{
    const boot_config_hdr_t *h;
    uint32_t total;

    if (boot_config_valid_at(BOOT_CONFIG_BASE0)) {
        return 0;                           /* 主区有效 */
    }
    if (!boot_config_valid_at(BOOT_CONFIG_BASE1)) {
        return -2;                          /* 两份都无效 */
    }

    h = (const boot_config_hdr_t *)(uintptr_t)BOOT_CONFIG_BASE1;
    total = (uint32_t)sizeof(*h) + h->len;
    if (boot_flash_erase(BOOT_CONFIG_BASE0, BOOT_CONFIG_SIZE) != 0) {
        return -1;
    }
    if (boot_flash_write(BOOT_CONFIG_BASE0, h, total) != 0) {
        return -1;
    }
    return boot_config_valid_at(BOOT_CONFIG_BASE0) ? 1 : -1;
}


