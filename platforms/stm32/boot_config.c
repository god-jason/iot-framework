/*
 * boot_config.c - STM32 配置区双备份（F1/F4 共用）
 *
 * 主区 BOOT_CONFIG_BASE0，备份 BOOT_CONFIG_BASE1（大小见 partition.h）；
 * 头部 boot_config_hdr_t 含 magic/长度/CRC，写入时先备份后主区。
 * Flash 擦写原语见 platforms/stm32<family>/boot_flash.c。
 */

#include "boot_config.h"
#include "boot_meta.h"
#include "boot_flash.h"

#include <stddef.h>

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
