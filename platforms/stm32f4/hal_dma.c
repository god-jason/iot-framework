/*
 * hal_dma.c - STM32F4 DMA HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static uint32_t stm32_dma_width(hal_dma_width_e width)
{
    switch (width) {
    case HAL_DMA_WIDTH_16BIT:
        return DMA_MEMORY_DATA_SIZE_HALFWORD;
    case HAL_DMA_WIDTH_32BIT:
        return DMA_MEMORY_DATA_SIZE_WORD;
    case HAL_DMA_WIDTH_8BIT:
    default:
        return DMA_MEMORY_DATA_SIZE_BYTE;
    }
}

static uint32_t stm32_dma_priority(hal_dma_priority_e priority)
{
    switch (priority) {
    case HAL_DMA_PRIORITY_MEDIUM:
        return DMA_PRIORITY_MEDIUM;
    case HAL_DMA_PRIORITY_HIGH:
        return DMA_PRIORITY_HIGH;
    case HAL_DMA_PRIORITY_VERY_HIGH:
        return DMA_PRIORITY_VERY_HIGH;
    case HAL_DMA_PRIORITY_LOW:
    default:
        return DMA_PRIORITY_LOW;
    }
}

int hal_dma_init(hal_dma_t *handle,
                            const hal_dma_config_t *config)
{
    DMA_HandleTypeDef *hdma;
    uint32_t periph_width;
    uint32_t mem_width;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    if ((hdma == NULL) || (hdma->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    memset(&hdma->Init, 0, sizeof(hdma->Init));

    switch (config->direction) {
    case HAL_DMA_MEMORY_TO_MEMORY:
        hdma->Init.Direction = DMA_MEMORY_TO_MEMORY;
        break;
    case HAL_DMA_MEMORY_TO_PERIPH:
        hdma->Init.Direction = DMA_MEMORY_TO_PERIPH;
        break;
    case HAL_DMA_PERIPH_TO_MEMORY:
        hdma->Init.Direction = DMA_PERIPH_TO_MEMORY;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    periph_width = stm32_dma_width(config->src_width);
    mem_width = stm32_dma_width(config->dst_width);

    if (config->direction == HAL_DMA_MEMORY_TO_PERIPH) {
        hdma->Init.PeriphInc = config->dst_increment ?
                               DMA_PINC_ENABLE : DMA_PINC_DISABLE;
        hdma->Init.MemInc = config->src_increment ?
                            DMA_MINC_ENABLE : DMA_MINC_DISABLE;
        hdma->Init.PeriphDataAlignment = mem_width;
        hdma->Init.MemDataAlignment = periph_width;
    } else if (config->direction == HAL_DMA_PERIPH_TO_MEMORY) {
        hdma->Init.PeriphInc = config->src_increment ?
                               DMA_PINC_ENABLE : DMA_PINC_DISABLE;
        hdma->Init.MemInc = config->dst_increment ?
                            DMA_MINC_ENABLE : DMA_MINC_DISABLE;
        hdma->Init.PeriphDataAlignment = periph_width;
        hdma->Init.MemDataAlignment = mem_width;
    } else {
        hdma->Init.PeriphInc = config->src_increment ?
                               DMA_PINC_ENABLE : DMA_PINC_DISABLE;
        hdma->Init.MemInc = config->dst_increment ?
                            DMA_MINC_ENABLE : DMA_MINC_DISABLE;
        hdma->Init.PeriphDataAlignment = periph_width;
        hdma->Init.MemDataAlignment = mem_width;
    }

    hdma->Init.Mode = (config->mode == HAL_DMA_MODE_CIRCULAR) ?
                      DMA_CIRCULAR : DMA_NORMAL;
    hdma->Init.Priority = stm32_dma_priority(config->priority);

    if (HAL_DMA_Init(hdma) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_dma_deinit(hal_dma_t *handle)
{
    DMA_HandleTypeDef *hdma;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }

    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    if (hdma != NULL) {
        (void)HAL_DMA_DeInit(hdma);
    }

    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_dma_start(hal_dma_t *handle, uintptr_t src,
                             uintptr_t dst, size_t length)
{
    DMA_HandleTypeDef *hdma;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    /*
     * 外设方向需要具体的 DMA 请求号/通道映射，公共 API 无法从 request
     * 字段安全推导，返回 NOT_SUPPORTED。M2M 可直接启动。
     */
    if (handle->config.direction != HAL_DMA_MEMORY_TO_MEMORY) {
        return HAL_NOT_SUPPORTED;
    }

    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    if (hdma == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY;
    status = HAL_DMA_Start(hdma, (uint32_t)src, (uint32_t)dst,
                           (uint32_t)length);
    if (status != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return ros_cube_status(status);
    }
    return HAL_OK;
}

int hal_dma_stop(hal_dma_t *handle)
{
    DMA_HandleTypeDef *hdma;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }

    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    if (hdma == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_DMA_Abort(hdma);
    handle->base.state = HAL_STATE_READY;
    return ros_cube_status(status);
}

int hal_dma_suspend(hal_dma_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

int hal_dma_resume(hal_dma_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

size_t hal_dma_get_remaining(hal_dma_t *handle)
{
    DMA_HandleTypeDef *hdma;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return 0U;
    }
    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    return (size_t)__HAL_DMA_GET_COUNTER(hdma);
}


#endif /* ROS_STM32_F4 */
