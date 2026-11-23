/*
 * hal_qspi.c - STM32F4 QSPI（STM32F4 不支持） HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

int hal_qspi_init(hal_qspi_t *handle,
                             const hal_qspi_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    return HAL_NOT_SUPPORTED;
}


int hal_qspi_deinit(hal_qspi_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_qspi_command(hal_qspi_t *handle,
                                const hal_qspi_command_t *command)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(command);
    return HAL_NOT_SUPPORTED;
}


int hal_qspi_transmit(hal_qspi_t *handle, const void *data,
                                 size_t length, uint32_t timeout)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    HAL_UNUSED(timeout);
    return HAL_NOT_SUPPORTED;
}


int hal_qspi_receive(hal_qspi_t *handle, void *data,
                                size_t length, uint32_t timeout)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    HAL_UNUSED(timeout);
    return HAL_NOT_SUPPORTED;
}


int hal_qspi_memory_map(hal_qspi_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_qspi_memory_unmap(hal_qspi_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


#endif /* ROS_STM32_F4 */
