/*
 * hal_flash.c - STM32F1 片内 Flash 寄存器实现（不依赖 Cube HAL）
 *
 * F103 中容量产品按 1KB 页擦除、16 位半字写入。本模块只操作主存储区，
 * 不处理选项字节与写保护。
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "stm32f10x.h"

#include <string.h>

#ifndef ROS_STM32_FLASH_SIZE
#define ROS_STM32_FLASH_SIZE (128U * 1024U)
#endif

#define STM32_FLASH_PAGE_SIZE   1024U
#define STM32_FLASH_HALFWORD    2U

#define STM32_FLASH_CR_PG       (1UL << 0)
#define STM32_FLASH_CR_PER      (1UL << 1)
#define STM32_FLASH_CR_STRT     (1UL << 6)
#define STM32_FLASH_CR_LOCK     (1UL << 7)

#define STM32_FLASH_SR_BSY      (1UL << 0)
#define STM32_FLASH_SR_PGERR    (1UL << 2)
#define STM32_FLASH_SR_WRPRTERR (1UL << 4)
#define STM32_FLASH_SR_EOP      (1UL << 5)

#define STM32_FLASH_TIMEOUT_MS  100U

static int stm32_flash_wait_idle(void)
{
    uint32_t start = hal_tick_get();

    while ((FLASH->SR & STM32_FLASH_SR_BSY) != 0U) {
        if ((uint32_t)(hal_tick_get() - start) >= STM32_FLASH_TIMEOUT_MS) {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

static void stm32_flash_clear_flags(void)
{
    FLASH->SR = STM32_FLASH_SR_EOP | STM32_FLASH_SR_PGERR |
                STM32_FLASH_SR_WRPRTERR;
}

static int stm32_flash_unlock_internal(void)
{
    if ((FLASH->CR & STM32_FLASH_CR_LOCK) == 0U) {
        return HAL_OK;
    }
    FLASH->KEYR = 0x45670123U;
    FLASH->KEYR = 0xCDEF89ABU;
    return ((FLASH->CR & STM32_FLASH_CR_LOCK) == 0U) ?
           HAL_OK : HAL_PERMISSION;
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
    handle->info.erase_size = STM32_FLASH_PAGE_SIZE;
    handle->info.program_size = STM32_FLASH_HALFWORD;
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

int hal_flash_get_info(hal_flash_t *handle, hal_flash_info_t *info)
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
    if ((address % STM32_FLASH_HALFWORD) != 0U) {
        return HAL_INVALID_PARAM;
    }

    result = stm32_flash_unlock_internal();
    if (result != HAL_OK) {
        return result;
    }

    FLASH->CR |= STM32_FLASH_CR_PG;
    stm32_flash_clear_flags();

    for (offset = 0U; offset < length; offset += STM32_FLASH_HALFWORD) {
        uint16_t value = 0xFFFFU;
        size_t chunk = HAL_MIN((size_t)STM32_FLASH_HALFWORD, length - offset);

        memcpy(&value, source + offset, chunk);
        *(volatile uint16_t *)(address + offset) = value;

        result = stm32_flash_wait_idle();
        if (result != HAL_OK) {
            break;
        }
        if ((FLASH->SR & (STM32_FLASH_SR_PGERR |
                          STM32_FLASH_SR_WRPRTERR)) != 0U) {
            result = HAL_IO_ERROR;
            break;
        }
        stm32_flash_clear_flags();
    }

    FLASH->CR &= ~STM32_FLASH_CR_PG;
    stm32_flash_clear_flags();
    FLASH->CR |= STM32_FLASH_CR_LOCK;
    return result;
}

int hal_flash_erase(hal_flash_t *handle, uintptr_t address, size_t length)
{
    uintptr_t end;
    uintptr_t current;

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
    if (((address % STM32_FLASH_PAGE_SIZE) != 0U) ||
        ((length % STM32_FLASH_PAGE_SIZE) != 0U)) {
        return HAL_INVALID_PARAM;
    }

    if (stm32_flash_unlock_internal() != HAL_OK) {
        return HAL_PERMISSION;
    }

    FLASH->CR |= STM32_FLASH_CR_PER;
    stm32_flash_clear_flags();

    for (current = address; current < address + length;
         current += STM32_FLASH_PAGE_SIZE) {
        FLASH->AR = (uint32_t)current;
        FLASH->CR |= STM32_FLASH_CR_STRT;

        if (stm32_flash_wait_idle() != HAL_OK) {
            FLASH->CR &= ~STM32_FLASH_CR_PER;
            FLASH->CR |= STM32_FLASH_CR_LOCK;
            return HAL_TIMEOUT;
        }
        if ((FLASH->SR & (STM32_FLASH_SR_PGERR |
                          STM32_FLASH_SR_WRPRTERR)) != 0U) {
            FLASH->CR &= ~STM32_FLASH_CR_PER;
            FLASH->CR |= STM32_FLASH_CR_LOCK;
            return HAL_IO_ERROR;
        }
        stm32_flash_clear_flags();
    }

    FLASH->CR &= ~STM32_FLASH_CR_PER;
    FLASH->CR |= STM32_FLASH_CR_LOCK;
    return HAL_OK;
}

int hal_flash_lock(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    FLASH->CR |= STM32_FLASH_CR_LOCK;
    return HAL_OK;
}

int hal_flash_unlock(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    return stm32_flash_unlock_internal();
}

#endif /* ROS_STM32_F1 */
