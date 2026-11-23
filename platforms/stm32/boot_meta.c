/*
 * boot_meta.c - STM32 启动状态、升级历史与 CRC32（F1/F4 共用）
 *
 * 芯片相关的 Flash 擦写原语位于 platforms/stm32<family>/boot_flash.c；
 * 分区地址与板级参数（BOOT_*）位于 platforms/stm32<family> 或 apps/<board> 中的 partition.h。
 *
 * 该文件同时编入 Bootloader 与 App：
 *   - Bootloader 构建使用 -nostdlib，因此这里不调用任何 libc 函数；
 *   - App 只用到 boot_confirm()，链接器会按
 *     -ffunction-sections --gc-sections 丢弃未使用的 Flash 函数。
 */

#include "boot_meta.h"
#include "boot_flash.h"

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
 * 二、启动状态与升级历史（Boot Meta 区）
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
 * 历史区写满时先擦除整个 Meta 区并重写当前状态（保留 ota/备份标志），
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
