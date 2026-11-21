/*
 * boot_meta.c - A/B OTA 启动元数据、CRC32、Flash 驱动与启动确认
 *
 * 该文件同时编入 Bootloader 与 App：
 *   - Bootloader 构建使用 -nostdlib，因此这里不调用任何 libc 函数；
 *   - App 只用到 boot_confirm() / boot_crash_count()，链接器会按
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

    if (len == 0U || addr < BOOT_SLOT_BASE0 ||
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

    if (data == NULL || len == 0U || addr < BOOT_SLOT_BASE0 ||
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
 * 三、启动元数据（校验 / 加载 / 保存）
 * ========================================================================== */

/* 检查一份元数据副本是否合法 */
bool boot_meta_valid(const boot_meta_t *m)
{
    if (m->magic != BOOT_META_MAGIC) {
        return false;
    }
    if (m->meta_crc != boot_crc32(m, (uint32_t)sizeof(*m) - 4U)) {
        return false;
    }
    if (m->active > 1U) {
        return false;
    }
    return (m->pending == BOOT_SLOT_NONE) || (m->pending <= 1U);
}

/*
 * 读取最新有效元数据。
 * 两页都无效（首次上电）时返回默认值：active=A、pending=NONE、crash=0。
 */
void boot_meta_load(boot_meta_t *m)
{
    const boot_meta_t *p0 = (const boot_meta_t *)(uintptr_t)BOOT_META_PAGE0;
    const boot_meta_t *p1 = (const boot_meta_t *)(uintptr_t)BOOT_META_PAGE1;
    bool v0 = boot_meta_valid(p0);
    bool v1 = boot_meta_valid(p1);

    if (m == NULL) {
        return;
    }
    /* seq 用有符号差值比较，允许回绕 */
    if (v0 && (!v1 || (int32_t)(p0->seq - p1->seq) >= 0)) {
        *m = *p0;
        return;
    }
    if (v1) {
        *m = *p1;
        return;
    }

    /* 逐字段赋默认值（不调用 memset，便于 -nostdlib 链接） */
    m->magic = BOOT_META_MAGIC;
    m->seq = 0U;
    m->active = 0U;
    m->pending = BOOT_SLOT_NONE;
    m->tries = 0U;
    m->crash = 0U;
    m->len[0] = 0U;
    m->len[1] = 0U;
    m->crc[0] = 0U;
    m->crc[1] = 0U;
    m->meta_crc = 0U;
}

/*
 * 保存元数据：seq 自增后写入与奇偶对应的页（两页轮换），写完回读校验。
 * 轮换写保证任一时刻至少有一页是完整的旧副本。
 */
int boot_meta_save(boot_meta_t *m)
{
    uint32_t page;
    uint32_t i;
    const uint16_t *hw;
    const uint16_t *verify;

    if (m == NULL) {
        return -1;
    }
    /* 注意：seq 必须先自增再算 CRC，否则 CRC 覆盖的是旧 seq，写回的副本永远校验失败 */
    m->magic = BOOT_META_MAGIC;
    m->seq++;
    m->meta_crc = boot_crc32(m, (uint32_t)sizeof(*m) - 4U);

    page = ((m->seq & 1U) != 0U) ? BOOT_META_PAGE0 : BOOT_META_PAGE1;
    if (boot_flash_erase(page, BOOT_FLASH_PAGE) != 0) {
        return -1;
    }
    hw = (const uint16_t *)m;
    for (i = 0U; i < (uint32_t)(sizeof(*m) / 2U); i++) {
        if (boot_flash_write(page + i * 2U, &hw[i], 2U) != 0) {
            return -1;
        }
    }

    verify = (const uint16_t *)(uintptr_t)page;
    for (i = 0U; i < (uint32_t)(sizeof(*m) / 2U); i++) {
        if (verify[i] != hw[i]) {
            return -1;                      /* 回读不一致：视为失败 */
        }
    }
    return 0;
}

/* ==========================================================================
 * 四、App 侧接口
 * ========================================================================== */

void boot_confirm(void)
{
    boot_meta_t m;

    boot_meta_load(&m);
    if (m.pending != BOOT_SLOT_NONE || m.tries != 0U || m.crash != 0U) {
        if (m.pending != BOOT_SLOT_NONE) {
            m.active = m.pending;
            m.pending = BOOT_SLOT_NONE;
        }
        m.tries = 0U;
        m.crash = 0U;
        (void)boot_meta_save(&m);
    }
}

/* ==========================================================================
 * 五、崩溃日志（BOOT_LOG_BASE 起 2KB 文本区，只保留最近一次崩溃）
 * ========================================================================== */

#define BOOT_LOG_STACK_WORDS    128U    /* 栈快照字数（512 字节） */
#define BOOT_LOG_LINE_SIZE      128U    /* 单行文本缓冲 */

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

/*
 * 写一行文本到日志区：必要时补一个空格使长度成为偶数（半字编程要求），
 * 返回新的写入偏移。空间不足时停止后续写入。
 */
static void log_emit(uint32_t *off, char *text)
{
    uint32_t n = 0U;

    while (text[n] != '\0') {
        n++;
    }
    if ((n & 1U) != 0U) {
        text[n++] = ' ';
        text[n] = '\0';
    }
    if (*off + n > BOOT_LOG_SIZE) {
        return;
    }
    (void)boot_flash_write(BOOT_LOG_BASE + *off, text, n);
    *off += n;
}

void boot_crash_log(const uint32_t *frame, uint32_t sp, uint32_t exc_return)
{
    boot_meta_t m;
    char *p;
    uint32_t off = 0U;
    uint32_t addr;
    uint32_t words;
    uint32_t i;

    if (frame == NULL) {
        return;
    }

    /* 1) 崩溃计数 +1 并保存（Bootloader 据此判断是否回滚分区） */
    boot_meta_load(&m);
    if (m.crash < 0xFFFFFFFFU) {
        m.crash++;
    }
    (void)boot_meta_save(&m);

    /* 2) 整区擦除：只保留本次崩溃记录，不做追加 */
    (void)boot_flash_erase(BOOT_LOG_BASE, BOOT_LOG_SIZE);

    /* 3) 摘要行：原因、所在槽、崩溃次数 */
    p = s_log_line;
    p = log_put_str(p, "reason=HARDFAULT slot=");
    *p++ = (SCB->VTOR == BOOT_SLOT_BASE1) ? 'B' : 'A';
    p = log_put_str(p, " crash=");
    p = log_put_dec(p, m.crash);
    p = log_put_str(p, "\r\n");
    *p = '\0';
    log_emit(&off, s_log_line);

    /* 4) 异常栈帧：PC/LR/SP/xPSR */
    p = s_log_line;
    p = log_put_str(p, "pc=");
    p = log_put_hex(p, frame[6]);
    p = log_put_str(p, " lr=");
    p = log_put_hex(p, frame[5]);
    p = log_put_str(p, " sp=");
    p = log_put_hex(p, sp);
    p = log_put_str(p, " xpsr=");
    p = log_put_hex(p, frame[7]);
    p = log_put_str(p, "\r\n");
    *p = '\0';
    log_emit(&off, s_log_line);

    /* 5) 通用寄存器 */
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
    p = log_put_str(p, "\r\n");
    *p = '\0';
    log_emit(&off, s_log_line);

    /* 6) 故障状态寄存器 */
    p = s_log_line;
    p = log_put_str(p, "cfsr=");
    p = log_put_hex(p, SCB->CFSR);
    p = log_put_str(p, " hfsr=");
    p = log_put_hex(p, SCB->HFSR);
    p = log_put_str(p, " excr=");
    p = log_put_hex(p, exc_return);
    p = log_put_str(p, "\r\n");
    *p = '\0';
    log_emit(&off, s_log_line);

    p = s_log_line;
    p = log_put_str(p, "mmfar=");
    p = log_put_hex(p, SCB->MMFAR);
    p = log_put_str(p, " bfar=");
    p = log_put_hex(p, SCB->BFAR);
    p = log_put_str(p, "\r\n");
    *p = '\0';
    log_emit(&off, s_log_line);

    /* 7) 栈快照：从异常栈指针起 128 字，每行 8 字（crash_decode.py 可扫描） */
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
        p = log_put_str(p, "\r\n");
        *p = '\0';
        log_emit(&off, s_log_line);
    }
}
