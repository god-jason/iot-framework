/*
 * hal_flash.c - STM32F4 FLASH HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static uint32_t stm32_flash_sector(uintptr_t address)
{
    uintptr_t offset = address - (uintptr_t)FLASH_BASE;

    if (offset < 0x10000U) {
        return (uint32_t)(offset / 0x4000U);
    }
    if (offset < 0x20000U) {
        return 4U;
    }
    return 5U + (uint32_t)((offset - 0x20000U) / 0x20000U);
}

static uintptr_t stm32_flash_sector_start(uint32_t sector)
{
    if (sector < 4U) {
        return (uintptr_t)FLASH_BASE + (uintptr_t)sector * 0x4000U;
    }
    if (sector == 4U) {
        return (uintptr_t)FLASH_BASE + 0x10000U;
    }
    return (uintptr_t)FLASH_BASE + 0x20000U +
           (uintptr_t)(sector - 5U) * 0x20000U;
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
    handle->info.erase_size = ROS_STM32_FLASH_SECTOR_SIZE;
    handle->info.program_size = 4U;
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
    if ((address % 4U) != 0U) {
        return HAL_INVALID_PARAM;
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return HAL_ERROR;
    }

    for (offset = 0U; offset < length; offset += 4U) {
        uint32_t value = 0xFFFFFFFFU;
        size_t chunk = HAL_MIN((size_t)4U, length - offset);

        memcpy(&value, source + offset, chunk);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              address + offset,
                              (uint64_t)value) != HAL_OK) {
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
    uint32_t first_sector;
    uint32_t last_sector;

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

    first_sector = stm32_flash_sector(address);
    last_sector = stm32_flash_sector(address + length - 1U);
    if (address != stm32_flash_sector_start(first_sector)) {
        return HAL_INVALID_PARAM;
    }
    if (stm32_flash_sector_start(last_sector) >= end) {
        return HAL_INVALID_PARAM;
    }

    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = first_sector;
    erase.NbSectors = last_sector - first_sector + 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return HAL_ERROR;
    }

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        (void)HAL_FLASH_Lock();
        return HAL_IO_ERROR;
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


#endif /* ROS_STM32_F4 */
