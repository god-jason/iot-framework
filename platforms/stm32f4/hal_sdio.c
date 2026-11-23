/*
 * hal_sdio.c - STM32F4 SDIO（STM32F4 不支持） HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

int hal_sdio_init(hal_sdio_t *handle,
                             const hal_sdio_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    return HAL_NOT_SUPPORTED;
}


int hal_sdio_deinit(hal_sdio_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}


int hal_sdio_read(hal_sdio_t *handle, uint32_t block,
                             void *buffer, size_t block_count,
                             uint32_t timeout)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(block);
    HAL_UNUSED(buffer);
    HAL_UNUSED(block_count);
    HAL_UNUSED(timeout);
    return HAL_NOT_SUPPORTED;
}


int hal_sdio_write(hal_sdio_t *handle, uint32_t block,
                              const void *buffer, size_t block_count,
                              uint32_t timeout)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(block);
    HAL_UNUSED(buffer);
    HAL_UNUSED(block_count);
    HAL_UNUSED(timeout);
    return HAL_NOT_SUPPORTED;
}


int hal_sdio_read_dma(hal_sdio_t *handle, uint32_t block,
                                 void *buffer, size_t block_count)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(block);
    HAL_UNUSED(buffer);
    HAL_UNUSED(block_count);
    return HAL_NOT_SUPPORTED;
}


int hal_sdio_write_dma(hal_sdio_t *handle, uint32_t block,
                                  const void *buffer, size_t block_count)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(block);
    HAL_UNUSED(buffer);
    HAL_UNUSED(block_count);
    return HAL_NOT_SUPPORTED;
}


#endif /* ROS_STM32_F4 */
