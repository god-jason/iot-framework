/*
 * hal_dac.c - STM32F4 DAC HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static uint32_t stm32_dac_channel(uint8_t channel)
{
    return (channel == 2U) ? DAC_CHANNEL_2 : DAC_CHANNEL_1;
}

static uint32_t stm32_dac_alignment(hal_dac_alignment_e alignment)
{
    switch (alignment) {
    case HAL_DAC_ALIGN_8BIT:
        return DAC_ALIGN_8B_R;
    case HAL_DAC_ALIGN_12BIT_LEFT:
        return DAC_ALIGN_12B_L;
    case HAL_DAC_ALIGN_12BIT_RIGHT:
    default:
        return DAC_ALIGN_12B_R;
    }
}

int hal_dac_init(hal_dac_t *handle,
                            const hal_dac_config_t *config)
{
    DAC_HandleTypeDef *hdac;
    DAC_ChannelConfTypeDef channel_config;
    uint32_t channel;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if ((config->channel != 1U) && (config->channel != 2U)) {
        return HAL_INVALID_PARAM;
    }

    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if ((hdac == NULL) || (hdac->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    if (HAL_DAC_Init(hdac) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    channel = stm32_dac_channel(config->channel);
    memset(&channel_config, 0, sizeof(channel_config));
    channel_config.DAC_Trigger = DAC_TRIGGER_NONE;
    channel_config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;

    if (HAL_DAC_ConfigChannel(hdac, &channel_config, channel) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_dac_deinit(hal_dac_t *handle)
{
    DAC_HandleTypeDef *hdac;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac != NULL) {
        (void)HAL_DAC_DeInit(hdac);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_dac_start(hal_dac_t *handle)
{
    DAC_HandleTypeDef *hdac;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(
        HAL_DAC_Start(hdac, stm32_dac_channel(handle->config.channel)));
}

int hal_dac_stop(hal_dac_t *handle)
{
    DAC_HandleTypeDef *hdac;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(
        HAL_DAC_Stop(hdac, stm32_dac_channel(handle->config.channel)));
}

int hal_dac_write(hal_dac_t *handle, uint32_t value)
{
    DAC_HandleTypeDef *hdac;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(HAL_DAC_SetValue(
        hdac, stm32_dac_channel(handle->config.channel),
        stm32_dac_alignment(handle->config.alignment), value));
}

int hal_dac_write_dma(hal_dac_t *handle,
                                 const uint32_t *buffer, size_t length)
{
    DAC_HandleTypeDef *hdac;

    if ((handle == NULL) || (buffer == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(HAL_DAC_Start_DMA(
        hdac, stm32_dac_channel(handle->config.channel),
        (uint32_t *)(uintptr_t)buffer, (uint32_t)length,
        stm32_dac_alignment(handle->config.alignment)));
}


#endif /* ROS_STM32_F4 */
