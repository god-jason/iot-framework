/*
 * stm32_config.c - STM32 配置存储板级实现（F1/F4 共用）
 *
 * 配置区由 boot_config.c 的双区备份逻辑管理：
 *   主区 BOOT_CONFIG_BASE0，备份 BOOT_CONFIG_BASE1（大小见 partition.h）；
 *   头部 boot_config_hdr_t 含 magic/长度/CRC，写入时先备份后主区。
 */

#include "boot_config.h"
#include "boot_flash.h"
#include "config.h"

#include <stddef.h>

/* 定位一个有效配置区，返回 payload 指针并输出长度 */
static const uint8_t *config_payload(uint32_t base, uint32_t *len)
{
    const boot_config_hdr_t *h = (const boot_config_hdr_t *)(uintptr_t)base;

    if (!boot_config_valid_at(base)) {
        return NULL;
    }
    *len = h->len;
    return (const uint8_t *)(uintptr_t)(base + sizeof(*h));
}

int port_config_read(void *buf, uint32_t max, uint32_t *len)
{
    const uint8_t *src;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t n = 0U;
    uint32_t i;

    if (buf == NULL) {
        return CONFIG_ERR;
    }
    src = config_payload(BOOT_CONFIG_BASE0, &n);
    if (src == NULL) {
        src = config_payload(BOOT_CONFIG_BASE1, &n);
    }
    if (src == NULL || n > max) {
        return CONFIG_ERR;
    }
    for (i = 0U; i < n; i++) {
        dst[i] = src[i];
    }
    if (len != NULL) {
        *len = n;
    }
    return CONFIG_OK;
}

int port_config_write(const void *data, uint32_t len)
{
    if (data == NULL && len != 0U) {
        return CONFIG_ERR;
    }
    return (boot_config_save(data, len) == 0) ? CONFIG_OK : CONFIG_ERR;
}

int port_config_erase(void)
{
    if (boot_flash_erase(BOOT_CONFIG_BASE0, BOOT_CONFIG_SIZE) != 0) {
        return CONFIG_ERR;
    }
    if (boot_flash_erase(BOOT_CONFIG_BASE1, BOOT_CONFIG_SIZE) != 0) {
        return CONFIG_ERR;
    }
    return CONFIG_OK;
}

bool port_config_valid(void)
{
    return boot_config_valid_at(BOOT_CONFIG_BASE0) ||
           boot_config_valid_at(BOOT_CONFIG_BASE1);
}
