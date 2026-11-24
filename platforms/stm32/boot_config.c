/*
 * boot_config.c - STM32 配置存储（F1/F4 共用）
 *
 * 把 components/config 的双区轮转追加日志绑定到本板配置区：
 *   读：直接按绝对地址读取 Flash（内存映射）；
 *   写/擦：boot_flash_write / boot_flash_erase（见 platforms/stm32<family>）。
 * 记录格式、CRC、Commit、序号与轮转逻辑全部在 config_store 中实现。
 */

#include "boot_config.h"
#include "boot_flash.h"
#include "config.h"

#include <stddef.h>

/* 单条记录数据上限：为记录头/Commit/对齐预留 64 字节 */
#define BOOT_CONFIG_MAX_DATA    (BOOT_CONFIG_SIZE - 64U)

/* 读取 Flash：内存映射直读（volatile 逐字节，避免被优化成 memcpy） */
static int boot_config_flash_read(uint32_t addr, void *buf, uint32_t size)
{
    volatile const uint8_t *src = (volatile const uint8_t *)(uintptr_t)addr;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t i;

    if (buf == NULL && size != 0U) {
        return -1;
    }
    for (i = 0U; i < size; i++) {
        dst[i] = src[i];
    }
    return 0;
}

static const config_store_ops_t s_ops = {
    boot_config_flash_read,
    boot_flash_write,
    boot_flash_erase
};

static const config_store_layout_t s_layout = {
    { BOOT_CONFIG_BASE0, BOOT_CONFIG_BASE1 },
    BOOT_CONFIG_SIZE,
    4U,                             /* 记录按 4 字节对齐 */
    BOOT_CONFIG_MAX_DATA,
    (uint16_t)BOOT_CONFIG_VERSION
};

static config_store_t s_store;
static uint8_t s_ready;

config_store_t *boot_config_storage(void)
{
    return &s_store;
}

int boot_config_init(void)
{
    int rc;

    if (s_ready != 0U) {
        return 0;
    }
    rc = config_store_init(&s_store, &s_layout, &s_ops);
    if (rc != CONFIG_STORE_OK && rc != CONFIG_STORE_NONE) {
        return -1;
    }
    s_ready = 1U;
    return 0;
}

int boot_config_load(void *data, uint32_t size)
{
    int rc;

    if (boot_config_init() != 0) {
        return -1;
    }
    rc = config_store_load(&s_store, data, size);
    if (rc == CONFIG_STORE_OK) {
        return 0;
    }
    return (rc == CONFIG_STORE_NONE) ? -2 : -1;
}

int boot_config_load_raw(void *buf, uint32_t cap, uint32_t *len,
                         uint16_t *version)
{
    int rc;

    if (boot_config_init() != 0) {
        return -1;
    }
    rc = config_store_load_raw(&s_store, buf, cap, len, version);
    if (rc == CONFIG_STORE_OK) {
        return 0;
    }
    return (rc == CONFIG_STORE_NONE) ? -2 : -1;
}

int boot_config_save(const void *data, uint32_t len)
{
    if (boot_config_init() != 0) {
        return -1;
    }
    return (config_store_save(&s_store, data, len) == CONFIG_STORE_OK)
           ? 0 : -1;
}

bool boot_config_valid(void)
{
    return (boot_config_init() == 0) && (s_store.has_record != 0U);
}

int boot_config_format(void)
{
    if (boot_config_init() != 0) {
        return -1;
    }
    return (config_store_format(&s_store) == CONFIG_STORE_OK) ? 0 : -1;
}

int boot_config_restore(void)
{
    if (boot_config_init() != 0) {
        return -1;
    }
    return (s_store.has_record != 0U) ? 0 : -2;
}
