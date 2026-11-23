/*
 * hal_mcu.c - STM32F1 CORE / PLATFORM 适配（纯寄存器）
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "stm32f10x.h"

#include <string.h>

#ifndef ROS_STM32_FLASH_SIZE
#define ROS_STM32_FLASH_SIZE (128U * 1024U)
#endif

#ifndef ROS_STM32_SRAM_SIZE
#define ROS_STM32_SRAM_SIZE (20U * 1024U)
#endif

uint32_t hal_get_capabilities(hal_device_type_e type, uint8_t instance)
{
    HAL_UNUSED(instance);

    switch (type) {
    case HAL_DEVICE_GPIO:
        return 0U;
    case HAL_DEVICE_UART:
        return HAL_CAP_IT;
    case HAL_DEVICE_SPI:
        return 0U;
    case HAL_DEVICE_I2C:
        return 0U;
    case HAL_DEVICE_CAN:
        return HAL_CAP_IT;
    case HAL_DEVICE_ADC:
        return HAL_CAP_IT;
    case HAL_DEVICE_DAC:
        return 0U;
    case HAL_DEVICE_PWM:
        return 0U;
    case HAL_DEVICE_TIMER:
        return HAL_CAP_IT;
    case HAL_DEVICE_FLASH:
        return HAL_FLASH_FLAG_READ | HAL_FLASH_FLAG_WRITE |
               HAL_FLASH_FLAG_ERASE;
    case HAL_DEVICE_USB:
    case HAL_DEVICE_ETH:
    default:
        return 0U;
    }
}

int hal_init(void)
{
    return HAL_OK;
}

int hal_deinit(void)
{
    return HAL_OK;
}

int hal_get_info(hal_mcu_info_t *info)
{
    if (info == NULL) {
        return HAL_INVALID_PARAM;
    }

    memset(info, 0, sizeof(*info));
    info->family = HAL_MCU_STM32F1;
    info->core_clock = SystemCoreClock;
    info->ahb_clock = SystemCoreClock;
    info->apb1_clock = SystemCoreClock / 2U;
    info->apb2_clock = SystemCoreClock;
    info->flash_size = ROS_STM32_FLASH_SIZE;
    info->sram_size = ROS_STM32_SRAM_SIZE;
    return HAL_OK;
}

#endif /* ROS_STM32_F1 */
