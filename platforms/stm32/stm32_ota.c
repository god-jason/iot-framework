/*
 * stm32_ota.c - STM32 FOTA 板级后端（F1/F4 共用）
 *
 * 实现 components/fota 的 port_ota_xxx：把原始固件写入 OTA 区，
 * 完成时校验 CRC 并更新 Boot Meta 中的启动状态（ota_valid/len/crc）。
 * boot_state_t.reserved（备份有效标志）保持原值不变。
 */

#include "boot_meta.h"
#include "board_flash.h"
#include "crc32.h"
#include "fota.h"

#include <stddef.h>

static uint32_t s_len;
static uint32_t s_crc;

int port_ota_begin(uint32_t image_size, uint32_t image_crc)
{
    boot_state_t s;

    if (image_size == 0U || image_size > FLASH_OTA_SIZE) {
        return FOTA_ERR;
    }
    /* 清除旧标识，避免 Bootloader 使用半写入的镜像 */
    boot_state_load(&s);
    s.ota_valid = 0U;
    s.ota_len = 0U;
    s.ota_crc = 0U;
    if (boot_state_save(&s) != 0) {
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
    return (board_flash_write(FLASH_OTA_BASE + offset, data, len) == 0)
           ? FOTA_OK : FOTA_ERR;
}

int port_ota_finish(void)
{
    boot_state_t s;

    if (crc32((const void *)(uintptr_t)FLASH_OTA_BASE, s_len) != s_crc) {
        return FOTA_ERR;
    }
    boot_state_load(&s);
    s.ota_valid = 1U;
    s.ota_len = s_len;
    s.ota_crc = s_crc;
    s.tries = 0U;
    s.crash = 0U;
    return (boot_state_save(&s) == 0) ? FOTA_OK : FOTA_ERR;
}

void port_ota_abort(void)
{
    boot_state_t s;

    boot_state_load(&s);
    s.ota_valid = 0U;
    s.ota_len = 0U;
    s.ota_crc = 0U;
    (void)boot_state_save(&s);
}

int port_ota_state(fota_state_t *state)
{
    boot_state_t s;

    if (state == NULL) {
        return FOTA_ERR;
    }
    boot_state_load(&s);
    state->ota_valid = (s.ota_valid != 0U);
    state->ota_len = s.ota_len;
    state->ota_crc = s.ota_crc;
    return FOTA_OK;
}
