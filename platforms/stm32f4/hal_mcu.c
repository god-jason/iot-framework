/*
 * hal_mcu.c - STM32F4 CORE / PLATFORM HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

uint32_t hal_get_capabilities(hal_device_type_e type, uint8_t instance)
{
    HAL_UNUSED(instance);

    switch (type) {
    case HAL_DEVICE_GPIO:
        return 0U;
    case HAL_DEVICE_UART:
        return HAL_CAP_DMA | HAL_CAP_IT;
    case HAL_DEVICE_SPI:
        return HAL_CAP_DMA;
    case HAL_DEVICE_I2C:
        return HAL_CAP_DMA;
    case HAL_DEVICE_CAN:
        return HAL_CAP_IT;
    case HAL_DEVICE_ADC:
        return HAL_CAP_DMA | HAL_CAP_IT;
    case HAL_DEVICE_DAC:
        return HAL_CAP_DMA;
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
    info->family = HAL_MCU_STM32F4;
    info->core_clock = 168000000U;
    info->ahb_clock = 168000000U;
    info->apb1_clock = 42000000U;
    info->apb2_clock = 84000000U;
    info->flash_size = ROS_STM32_FLASH_SIZE;
    info->sram_size = 192U * 1024U;
    return HAL_OK;
}

#endif /* ROS_STM32_F4 */
