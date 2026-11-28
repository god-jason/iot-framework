/*
 * stm32_ota.c - STM32 FOTA 板级后端（F1/F4 共用）
 *
 * 实现 src/fota 的 port_ota_xxx：把 App 传入的原始固件写入 OTA 区，
 * OTA 区格式见 src/fota/fota.h：
 *
 *   OTA 区 = fota_hdr_t（32B） + 原始镜像
 *
 * 包头在负载全部写完且 CRC 校验通过之后才写入，因此升级中断时 OTA 区
 * 没有有效魔术字，Bootloader 会忽略它，下次重新升级即可。
 * Bootloader 每次启动自行检测包头，升级成功后擦除 OTA 区；
 * 本文件不再读写 KV 中的启动状态。
 */

#include "board_flash.h"
#include "crc32.h"
#include "fota.h"
#include "partition.h"

#include <stddef.h>

static uint32_t s_len;
static uint32_t s_crc;

int port_ota_begin(uint32_t image_size, uint32_t image_crc)
{
    if (image_size == 0U || image_size > FLASH_APP_SIZE ||
        (image_size + FOTA_HDR_SIZE) > FLASH_OTA_SIZE) {
        return FOTA_ERR;
    }
    if (board_flash_erase(FLASH_OTA_BASE, FLASH_OTA_SIZE) != 0) {
        return FOTA_ERR;
    }
    s_len = image_size;
    s_crc = image_crc;
    return FOTA_OK;
}

int port_ota_write(uint32_t offset, const void *data, uint32_t len)
{
    if (offset > s_len || len > (s_len - offset)) {
        return FOTA_ERR;
    }
    return (board_flash_write(FLASH_OTA_BASE + FOTA_HDR_SIZE + offset,
                              data, len) == 0) ? FOTA_OK : FOTA_ERR;
}

int port_ota_finish(void)
{
    fota_hdr_t h;

    if (crc32((const void *)(uintptr_t)(FLASH_OTA_BASE + FOTA_HDR_SIZE),
              s_len) != s_crc) {
        return FOTA_ERR;
    }

    h.flags = 0U;
    h.image_len = s_len;
    h.image_crc = s_crc;
    h.data_len = s_len;
    h.data_crc = s_crc;
    fota_hdr_seal(&h);

    return (board_flash_write(FLASH_OTA_BASE, &h, FOTA_HDR_SIZE) == 0)
           ? FOTA_OK : FOTA_ERR;
}

void port_ota_abort(void)
{
    /* 包头尚未写入（或已被 begin 擦除），Bootloader 会忽略 OTA 区 */
}

int port_ota_state(fota_state_t *state)
{
    const fota_hdr_t *h = (const fota_hdr_t *)(uintptr_t)FLASH_OTA_BASE;

    if (state == NULL) {
        return FOTA_ERR;
    }
    state->ota_valid = false;
    state->ota_len = 0U;
    state->ota_crc = 0U;

    if (!fota_hdr_valid(h) || h->image_len > FLASH_APP_SIZE ||
        h->data_len > (FLASH_OTA_SIZE - FOTA_HDR_SIZE)) {
        return FOTA_OK;
    }
    if (crc32((const void *)(uintptr_t)(FLASH_OTA_BASE + FOTA_HDR_SIZE),
              h->data_len) != h->data_crc) {
        return FOTA_OK;
    }

    state->ota_valid = true;
    state->ota_len = h->image_len;
    state->ota_crc = h->image_crc;
    return FOTA_OK;
}
