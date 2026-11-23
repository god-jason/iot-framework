/*
 * hal_eth.c - STM32F4 ETH（STM32F4 不支持） HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

int hal_eth_init(hal_eth_t *handle,
                            const hal_eth_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    return HAL_NOT_SUPPORTED;
}


int hal_eth_deinit(hal_eth_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_eth_start(hal_eth_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_eth_stop(hal_eth_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_eth_transmit(hal_eth_t *handle, const void *data,
                                size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}


int hal_eth_receive(hal_eth_t *handle, void *buffer,
                               size_t buffer_size, size_t *received_length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(buffer);
    HAL_UNUSED(buffer_size);
    HAL_UNUSED(received_length);
    return HAL_NOT_SUPPORTED;
}


#endif /* ROS_STM32_F4 */
