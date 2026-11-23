/*
 * hal_flash.c - STM32H7 FLASH HAL 适配（HAL2）
 *
 * H7 的 Flash 按 128KB 扇区、双 Bank 组织，写入粒度为 256 位 FlashWord
 * （32 字节）。本模块按公共 hal_flash 接口做最小封装，未启用 ECC 校验与
 * 写保护选项字节。
 */

#if defined(ROS_STM32_H7)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

#define STM32_FLASH_BANK_SIZE   (1024U * 1024U)
#define STM32_FLASH_SECTOR_SIZE (128U * 1024U)
#define STM32_FLASH_WORD_SIZE   32U

#ifdef FLASH_BANK_2
#define STM32_FLASH_LAST_BANK   FLASH_BANK_2
#else
#define STM32_FLASH_LAST_BANK   FLASH_BANK_1
#endif

typedef struct {
    uint32_t bank;
    uint32_t sector;
} stm32_flash_loc_t;

static stm32_flash_loc_t stm32_flash_locate(uintptr_t address)
{
    stm32_flash_loc_t loc;
    uintptr_t offset = address - (uintptr_t)FLASH_BASE;

    loc.bank = (offset >= STM32_FLASH_BANK_SIZE) ? STM32_FLASH_LAST_BANK
                                                 : FLASH_BANK_1;
    loc.sector = (uint32_t)((offset % STM32_FLASH_BANK_SIZE) /
                            STM32_FLASH_SECTOR_SIZE);
    return loc;
}

static uintptr_t stm32_flash_sector_start(uint32_t bank, uint32_t sector)
{
    uintptr_t base = (uintptr_t)FLASH_BASE;

    if (bank == STM32_FLASH_LAST_BANK) {
        base += STM32_FLASH_BANK_SIZE;
    }
    return base + (uintptr_t)sector * STM32_FLASH_SECTOR_SIZE;
}

int hal_flash_init(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    memset(&handle->info, 0, sizeof(handle->info));
    handle->info.start = (uintptr_t)FLASH_BASE;
    handle->info.size = ROS_STM32_FLASH_SIZE;
    handle->info.erase_size = STM32_FLASH_SECTOR_SIZE;
    handle->info.program_size = STM32_FLASH_WORD_SIZE;
    handle->info.flags = HAL_FLASH_FLAG_READ | HAL_FLASH_FLAG_WRITE |
                         HAL_FLASH_FLAG_ERASE;

    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_flash_deinit(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_flash_get_info(hal_flash_t *handle,
                       hal_flash_info_t *info)
{
    if ((handle == NULL) || (info == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    *info = handle->info;
    return HAL_OK;
}

int hal_flash_read(hal_flash_t *handle, uintptr_t address,
                   void *buffer, size_t length)
{
    uintptr_t end;

    if ((handle == NULL) || (buffer == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }

    end = handle->info.start + handle->info.size;
    if ((address < handle->info.start) || (address >= end) ||
        (length > (size_t)(end - address))) {
        return HAL_INVALID_PARAM;
    }

    memcpy(buffer, (const void *)address, length);
    return HAL_OK;
}

int hal_flash_write(hal_flash_t *handle, uintptr_t address,
                    const void *buffer, size_t length)
{
    const uint8_t *source = (const uint8_t *)buffer;
    uintptr_t end;
    size_t offset;
    int result = HAL_OK;

    if ((handle == NULL) || (buffer == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }

    end = handle->info.start + handle->info.size;
    if ((address < handle->info.start) || (address >= end) ||
        (length > (size_t)(end - address))) {
        return HAL_INVALID_PARAM;
    }
    if ((address % STM32_FLASH_WORD_SIZE) != 0U) {
        return HAL_INVALID_PARAM;
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return HAL_ERROR;
    }

    for (offset = 0U; offset < length; offset += STM32_FLASH_WORD_SIZE) {
        uint32_t word[STM32_FLASH_WORD_SIZE / 4U] __attribute__((aligned(8)));
        size_t chunk = HAL_MIN((size_t)STM32_FLASH_WORD_SIZE, length - offset);

        memset(word, 0xFF, sizeof(word));
        memcpy(word, source + offset, chunk);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                              address + offset,
                              (uint32_t)(uintptr_t)word) != HAL_OK) {
            result = HAL_IO_ERROR;
            break;
        }
    }

    (void)HAL_FLASH_Lock();
    return result;
}

int hal_flash_erase(hal_flash_t *handle, uintptr_t address,
                    size_t length)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0U;
    uintptr_t end;
    stm32_flash_loc_t first;
    stm32_flash_loc_t last;
    uint32_t bank;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }

    end = handle->info.start + handle->info.size;
    if ((address < handle->info.start) || (address >= end) ||
        (length > (size_t)(end - address))) {
        return HAL_INVALID_PARAM;
    }

    first = stm32_flash_locate(address);
    last = stm32_flash_locate(address + length - 1U);
    if (address != stm32_flash_sector_start(first.bank, first.sector)) {
        return HAL_INVALID_PARAM;
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return HAL_ERROR;
    }

    /* 跨 Bank 时分别擦除两个 Bank 的扇区范围。 */
    for (bank = first.bank; ; bank++) {
        uint32_t first_sector = (bank == first.bank) ? first.sector : 0U;
        uint32_t last_sector = (bank == last.bank) ?
                               last.sector : 7U;

        memset(&erase, 0, sizeof(erase));
        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Banks = bank;
        erase.Sector = first_sector;
        erase.NbSectors = last_sector - first_sector + 1U;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

        if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
            (void)HAL_FLASH_Lock();
            return HAL_IO_ERROR;
        }
        if (bank == last.bank) {
            break;
        }
    }

    (void)HAL_FLASH_Lock();
    return HAL_OK;
}

int hal_flash_lock(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    (void)HAL_FLASH_Lock();
    return HAL_OK;
}

int hal_flash_unlock(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    return ros_cube_status(HAL_FLASH_Unlock());
}

#endif /* ROS_STM32_H7 */
