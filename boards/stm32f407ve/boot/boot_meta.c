/*
 * boot_meta.c - STM32F407VE A/B/C OTA 元数据、CRC32、Flash 驱动与崩溃日志
 *
 * 元数据改为 Meta 日志区（S1，16KB）的 64B 追加记录：每次保存只写一条
 * 新记录、不擦除旧记录，掉电时最多丢失正在写的那一条；记录写满后才整区
 * 擦除并重写最新状态。
 *
 * 系统日志（S4，64KB）同样采用追加写入，每条以 "[unix] " 开头，写满后
 * 整区擦除从头部重新写入。
 *
 * 该文件同时编入 Bootloader 与 App，不调用 libc（Bootloader 用 -nostdlib）。
 */

#include "boot_meta.h"

#include <stddef.h>

/* ==========================================================================
 * 一、CRC32（CRC-32/IEEE，多项式 0xEDB88320，无查表）
 * ========================================================================== */

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

uint32_t boot_crc32(const void *data, uint32_t len)
{
    return boot_crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}

/* ==========================================================================
 * 二、F4 Flash 驱动（扇区擦除、字节编程）
 * ========================================================================== */

#define BOOT_FLASH_CR_PG        (1UL << 0)
#define BOOT_FLASH_CR_SER       (1UL << 1)
#define BOOT_FLASH_CR_SNB       (0xFUL << 3)
#define BOOT_FLASH_CR_PSIZE_8   (0UL << 8)
#define BOOT_FLASH_CR_STRT      (1UL << 16)
#define BOOT_FLASH_CR_LOCK      (1UL << 31)

#define BOOT_FLASH_SR_BSY       (1UL << 16)
#define BOOT_FLASH_SR_ERRORS    0x000002F2UL   /* OPERR/WRPERR/PGAERR/PGPERR/PGSERR/RDERR */
#define BOOT_FLASH_TIMEOUT      0x400000UL

/* STM32F407VE 512KB 扇区表 */
static const uint32_t s_sector_base[] = {
    0x08000000U, 0x08004000U, 0x08008000U, 0x0800C000U,
    0x08010000U, 0x08020000U, 0x08040000U, 0x08060000U
};

static const uint32_t s_sector_size[] = {
    16U * 1024U, 16U * 1024U, 16U * 1024U, 16U * 1024U,
    64U * 1024U, 128U * 1024U, 128U * 1024U, 128U * 1024U
};

static int boot_flash_wait(void)
{
    uint32_t timeout = BOOT_FLASH_TIMEOUT;

    while ((FLASH->SR & BOOT_FLASH_SR_BSY) != 0U) {
        if (--timeout == 0U) {
            return -1;
        }
    }
    if ((FLASH->SR & BOOT_FLASH_SR_ERRORS) != 0U) {
        FLASH->SR = BOOT_FLASH_SR_ERRORS;    /* 写 1 清除 */
        return -1;
    }
    return 0;
}

static void boot_flash_unlock(void)
{
    if ((FLASH->CR & BOOT_FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
}

static void boot_flash_lock(void)
{
    FLASH->CR |= BOOT_FLASH_CR_LOCK;
}

static int boot_sector_index(uint32_t addr)
{
    uint32_t i;

    for (i = 0U; i < sizeof(s_sector_base) / sizeof(s_sector_base[0]); i++) {
        if (addr >= s_sector_base[i] &&
            addr < s_sector_base[i] + s_sector_size[i]) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * 擦除覆盖 [addr, addr+len) 的 Flash 扇区。
 * addr 必须位于扇区起始；S0（Bootloader）不可擦除。
 */
int boot_flash_erase(uint32_t addr, uint32_t len)
{
    uint32_t end;
    uint32_t cur;
    int rc = 0;

    if (len == 0U || addr < BOOT_META_BASE) {
        return -1;
    }

    end = addr + len;
    cur = addr;
    boot_flash_unlock();
    while (cur < end) {
        int idx = boot_sector_index(cur);
        uint32_t base;

        if (idx < 0 || cur != s_sector_base[idx]) {
            rc = -1;
            break;
        }
        base = s_sector_base[idx];
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOOT_FLASH_CR_PSIZE_8 | BOOT_FLASH_CR_SER |
                    ((uint32_t)idx << 3);
        FLASH->CR |= BOOT_FLASH_CR_STRT;
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = 0U;
        cur = base + s_sector_size[idx];
    }
    boot_flash_lock();
    return rc;
}

/* 从 addr 开始写入 len 字节（字节编程，调用前已异步擦除） */
int boot_flash_write(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;
    int rc = 0;

    if (data == NULL || len == 0U || addr < BOOT_META_BASE) {
        return -1;
    }

    boot_flash_unlock();
    for (i = 0U; i < len; i++) {
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOOT_FLASH_CR_PSIZE_8 | BOOT_FLASH_CR_PG;
        *(volatile uint8_t *)(uintptr_t)(addr + i) = p[i];
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

static const boot_meta_t *meta_record(uint32_t index)
{
    return (const boot_meta_t *)(uintptr_t)
           (BOOT_META_BASE + index * BOOT_META_SLOT);
}

static void meta_copy(boot_meta_t *dst, const boot_meta_t *src)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0U; i < (uint32_t)sizeof(*dst); i++) {
        d[i] = s[i];
    }
}

bool boot_meta_valid(const boot_meta_t *m)
{
    if (m->magic != BOOT_META_MAGIC) {
        return false;
    }
    if (m->meta_crc != boot_crc32(m, (uint32_t)sizeof(*m) - 4U)) {
        return false;
    }
    return (m->state <= BOOT_STATE_TRYING) && (m->b_valid <= 1U);
}

/* 扫描 Meta 日志区，取 seq 最大且 CRC 有效的记录 */
void boot_meta_load(boot_meta_t *m)
{
    const boot_meta_t *best = NULL;
    uint32_t i;

    if (m == NULL) {
        return;
    }
    for (i = 0U; i < BOOT_META_RECORDS; i++) {
        const boot_meta_t *p = meta_record(i);

        if (!boot_meta_valid(p)) {
            continue;
        }
        if (best == NULL || (int32_t)(p->seq - best->seq) > 0) {
            best = p;
        }
    }
    if (best != NULL) {
        meta_copy(m, best);
        return;
    }

    m->magic = BOOT_META_MAGIC;
    m->seq = 0U;
    m->state = BOOT_STATE_IDLE;
    m->tries = 0U;
    m->crash = 0U;
    m->b_valid = 0U;
    for (i = 0U; i < (uint32_t)(sizeof(m->reserved) / sizeof(m->reserved[0])); i++) {
        m->reserved[i] = 0U;
    }
    m->meta_crc = 0U;
}

/* 保存元数据：追加一条新记录；区域写满后整区擦除再写 */
int boot_meta_save(boot_meta_t *m)
{
    const uint8_t *src;
    const uint8_t *verify;
    uint32_t addr = 0U;
    uint32_t i;

    if (m == NULL) {
        return -1;
    }
    m->magic = BOOT_META_MAGIC;
    m->seq++;
    m->meta_crc = boot_crc32(m, (uint32_t)sizeof(*m) - 4U);

    for (i = 0U; i < BOOT_META_RECORDS; i++) {
        if (meta_record(i)->magic != BOOT_META_MAGIC) {
            addr = BOOT_META_BASE + i * BOOT_META_SLOT;
            break;
        }
    }
    if (addr == 0U) {
        if (boot_flash_erase(BOOT_META_BASE, BOOT_META_SIZE) != 0) {
            return -1;
        }
        addr = BOOT_META_BASE;
    }
    if (boot_flash_write(addr, m, (uint32_t)sizeof(*m)) != 0) {
        return -1;
    }

    src = (const uint8_t *)m;
    verify = (const uint8_t *)(uintptr_t)addr;
    for (i = 0U; i < (uint32_t)sizeof(*m); i++) {
        if (verify[i] != src[i]) {
            return -1;
        }
    }
    return 0;
}

/* ==========================================================================
 * 四、App 侧接口
 * ========================================================================== */

/* App 初始化完成：确认本次启动成功，结束试运行状态 */
void boot_confirm(void)
{
    boot_meta_t m;

    boot_meta_load(&m);
    if (m.state == BOOT_STATE_TRYING || m.tries != 0U || m.crash != 0U) {
        m.state = BOOT_STATE_IDLE;
        m.tries = 0U;
        m.crash = 0U;
        /* 新固件已确认：B 区备份作废，下次升级时重新备份当时的 A 区 */
        m.b_valid = 0U;
        (void)boot_meta_save(&m);
    }
}

/* ==========================================================================
 * 五、系统日志（BOOT_LOG_BASE 起 64KB 文本，追加写入，带 Unix 时间）
 *
 * 每条格式：[<unix>] <text>\r\n
 * 空间不足时整区擦除后从头部重新追加；掉电最多丢失正在写的一行。
 * ========================================================================== */

#define BOOT_LOG_STACK_WORDS    128U
#define BOOT_LOG_LINE_SIZE      160U
#define BOOT_SRAM_END           0x20020000U   /* F407VE 128KB SRAM 顶端 */

static char s_log_line[BOOT_LOG_LINE_SIZE];

static char *log_put_str(char *p, const char *s)
{
    while (*s != '\0') {
        *p++ = *s++;
    }
    return p;
}

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
    while (*text != '\0' && (uint32_t)(p - line) < (BOOT_LOG_LINE_SIZE - 3U)) {
        *p++ = *text++;
    }
    *p++ = '\r';
    *p++ = '\n';
    n = (uint32_t)(p - line);

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
    boot_meta_t m;
    char *p;
    uint32_t addr;
    uint32_t words;
    uint32_t i;

    if (frame == NULL) {
        return;
    }

    boot_meta_load(&m);
    if (m.crash < 0xFFFFFFFFU) {
        m.crash++;
    }
    (void)boot_meta_save(&m);

    p = s_log_line;
    p = log_put_str(p, "reason=HARDFAULT slot=A");
    p = log_put_str(p, " crash=");
    p = log_put_dec(p, m.crash);
    *p = '\0';
    boot_log_append(unix_time, s_log_line);

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
        return 0;
    }
    if (!boot_config_valid_at(BOOT_CONFIG_BASE1)) {
        return -2;
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
