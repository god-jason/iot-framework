/*
 * board_ota.c - STM32F103CBT6 FOTA 板级后端
 *
 * 实现 components/fota 的 board_ota_xxx：把原始固件写入 OTA 区，
 * 完成时校验 CRC 并更新 Boot Meta 中的启动状态（ota_valid/len/crc）。
 */

#include "boot_meta.h"
#include "fota.h"

#include <stddef.h>

static uint32_t s_len;
static uint32_t s_crc;

int board_ota_begin(uint32_t image_size, uint32_t image_crc)
{
    boot_state_t s;

    if (image_size == 0U || image_size > BOOT_OTA_SIZE) {
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
    if (boot_flash_erase(BOOT_OTA_BASE, BOOT_OTA_SIZE) != 0) {
        return FOTA_ERR;
    }
    s_len = image_size;
    s_crc = image_crc;
    return FOTA_OK;
}

int board_ota_write(uint32_t offset, const void *data, uint32_t len)
{
    if (offset > s_len || len > (s_len - offset)) {
        return FOTA_ERR;
    }
    return (boot_flash_write(BOOT_OTA_BASE + offset, data, len) == 0)
           ? FOTA_OK : FOTA_ERR;
}

int board_ota_finish(void)
{
    boot_state_t s;

    if (boot_crc32((const void *)(uintptr_t)BOOT_OTA_BASE, s_len) != s_crc) {
        return FOTA_ERR;
    }
    boot_state_load(&s);
    s.ota_valid = 1U;
    s.ota_len = s_len;
    s.ota_crc = s_crc;
    s.tries = 0U;
    s.crash = 0U;
    s.reserved = 0U;
    return (boot_state_save(&s) == 0) ? FOTA_OK : FOTA_ERR;
}

void board_ota_abort(void)
{
    boot_state_t s;

    boot_state_load(&s);
    s.ota_valid = 0U;
    s.ota_len = 0U;
    s.ota_crc = 0U;
    (void)boot_state_save(&s);
}

int board_ota_state(fota_state_t *state)
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
