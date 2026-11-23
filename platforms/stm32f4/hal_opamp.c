/*
 * hal_opamp.c - STM32F4 OPAMP（F4 无此模块） HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

int hal_opamp_init(hal_opamp_t *handle,
                              const hal_opamp_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    return HAL_NOT_SUPPORTED;
}

int hal_opamp_start(hal_opamp_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

int hal_opamp_stop(hal_opamp_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


#endif /* ROS_STM32_F4 */
