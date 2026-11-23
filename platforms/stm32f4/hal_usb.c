/*
 * hal_usb.c - STM32F4 USB（STM32F4 不支持） HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

int hal_usb_init(hal_usb_t *handle,
                            const hal_usb_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    /* 简化实现：F4 USB PCD 由用户工程自行初始化。 */
    return HAL_NOT_SUPPORTED;
}


int hal_usb_deinit(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_usb_start(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_usb_stop(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_usb_connect(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_usb_disconnect(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


#endif /* ROS_STM32_F4 */
