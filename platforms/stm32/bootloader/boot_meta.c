/*
 * boot_meta.c - STM32 启动状态、OTA 进度与 CRC32（F1/F4 共用）
 *
 * 启动状态与升级进度均存于 KV 区（components/fkv）：
 *   BOOT_KV_KEY_STATE : boot_state_t
 *   BOOT_KV_KEY_OTA   : uint32_t（已写入 App 的字节数）
 * 单条记录自带 CRC + Commit，掉电只会丢弃未提交的新值，旧值仍可读。
 *
 * 该文件同时编入 Bootloader 与 App：
 *   - Bootloader 构建使用 -nostdlib，因此这里不调用任何 libc 函数；
 *   - App 只用到 boot_confirm()，链接器会按
 *     -ffunction-sections --gc-sections 丢弃未使用的函数。
 */

#include "boot_meta.h"
#include "boot_kv.h"

#include "crc32.h"

#include <stddef.h>

/* ==========================================================================
 * 一、启动状态（KV）
 * ========================================================================== */

static void state_defaults(boot_state_t *s)
{
    s->magic = BOOT_STATE_MAGIC;
    s->tries = 0U;
    s->crash = 0U;
    s->ota_valid = 0U;
    s->ota_len = 0U;
    s->ota_crc = 0U;
    s->reserved = 0U;
    s->crc = 0U;
}

bool boot_state_valid(const boot_state_t *s)
{
    if (s->magic != BOOT_STATE_MAGIC) {
        return false;
    }
    if (s->crc != crc32(s, (uint32_t)sizeof(*s) - 4U)) {
        return false;
    }
    return s->ota_valid <= 1U;
}

void boot_state_load(boot_state_t *s)
{
    uint32_t len = 0U;

    if (s == NULL) {
        return;
    }
    if (boot_kv_get(BOOT_KV_KEY_STATE, s, (uint32_t)sizeof(*s), &len) == 0 &&
        len == (uint32_t)sizeof(*s) && boot_state_valid(s)) {
        return;
    }
    state_defaults(s);
}

int boot_state_save(boot_state_t *s)
{
    if (s == NULL) {
        return -1;
    }
    s->magic = BOOT_STATE_MAGIC;
    s->crc = crc32(s, (uint32_t)sizeof(*s) - 4U);

    if (boot_kv_put(BOOT_KV_KEY_STATE, s, (uint32_t)sizeof(*s)) != 0) {
        return -1;
    }
    /* 与旧的“擦除 Meta 区”语义一致：状态保存即清空升级进度 */
    (void)boot_kv_del(BOOT_KV_KEY_OTA);
    return 0;
}

/* ==========================================================================
 * 二、OTA 进度（KV）
 * ========================================================================== */

bool boot_history_last_length(uint32_t *length)
{
    uint32_t value = 0U;
    uint32_t len = 0U;

    if (length == NULL) {
        return false;
    }
    if (boot_kv_get(BOOT_KV_KEY_OTA, &value, (uint32_t)sizeof(value),
                    &len) != 0 ||
        len != (uint32_t)sizeof(value)) {
        return false;
    }
    *length = value;
    return true;
}

int boot_history_append(const boot_state_t *s, uint32_t length)
{
    (void)s;
    return boot_kv_put(BOOT_KV_KEY_OTA, &length, (uint32_t)sizeof(length));
}

/* ==========================================================================
 * 三、App 启动确认
 * ========================================================================== */

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
