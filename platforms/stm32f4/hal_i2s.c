/*
 * hal_i2s.c - STM32F4 I2S HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

int hal_i2s_init(hal_i2s_t *handle,
                            const hal_i2s_config_t *config)
{
    I2S_HandleTypeDef *hi2s;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if ((hi2s == NULL) || (hi2s->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    memset(&hi2s->Init, 0, sizeof(hi2s->Init));

    switch (config->mode) {
    case HAL_I2S_MODE_MASTER_RX:
        hi2s->Init.Mode = I2S_MODE_MASTER_RX;
        break;
    case HAL_I2S_MODE_SLAVE_TX:
        hi2s->Init.Mode = I2S_MODE_SLAVE_TX;
        break;
    case HAL_I2S_MODE_SLAVE_RX:
        hi2s->Init.Mode = I2S_MODE_SLAVE_RX;
        break;
    case HAL_I2S_MODE_MASTER_TX:
    default:
        hi2s->Init.Mode = I2S_MODE_MASTER_TX;
        break;
    }

    switch (config->standard) {
    case HAL_I2S_STANDARD_MSB:
        hi2s->Init.Standard = I2S_STANDARD_MSB;
        break;
    case HAL_I2S_STANDARD_LSB:
        hi2s->Init.Standard = I2S_STANDARD_LSB;
        break;
    case HAL_I2S_STANDARD_PCM:
        hi2s->Init.Standard = I2S_STANDARD_PCM_SHORT;
        break;
    case HAL_I2S_STANDARD_PHILIPS:
    default:
        hi2s->Init.Standard = I2S_STANDARD_PHILIPS;
        break;
    }

    switch (config->data_size) {
    case HAL_I2S_DATA_24BIT:
        hi2s->Init.DataFormat = I2S_DATAFORMAT_24B;
        break;
    case HAL_I2S_DATA_32BIT:
        hi2s->Init.DataFormat = I2S_DATAFORMAT_32B;
        break;
    case HAL_I2S_DATA_16BIT:
    default:
        hi2s->Init.DataFormat = I2S_DATAFORMAT_16B;
        break;
    }

    hi2s->Init.MCLKOutput = config->master_clock ?
                            I2S_MCLKOUTPUT_ENABLE : I2S_MCLKOUTPUT_DISABLE;
    hi2s->Init.AudioFreq = config->sample_rate;
    hi2s->Init.CPOL = I2S_CPOL_LOW;

    if (HAL_I2S_Init(hi2s) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_i2s_deinit(hal_i2s_t *handle)
{
    I2S_HandleTypeDef *hi2s;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s != NULL) {
        (void)HAL_I2S_DeInit(hi2s);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_i2s_transmit(hal_i2s_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    I2S_HandleTypeDef *hi2s;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_I2S_Transmit(hi2s, (uint16_t *)data, (uint16_t)length,
                              timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return ros_cube_status(status);
}

int hal_i2s_receive(hal_i2s_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    I2S_HandleTypeDef *hi2s;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_RX;
    status = HAL_I2S_Receive(hi2s, (uint16_t *)data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return ros_cube_status(status);
}

int hal_i2s_transmit_dma(hal_i2s_t *handle,
                                    const void *data, size_t length)
{
    I2S_HandleTypeDef *hi2s;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_I2S_Transmit_DMA(hi2s, (uint16_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return ros_cube_status(status);
}

int hal_i2s_receive_dma(hal_i2s_t *handle, void *data,
                                   size_t length)
{
    I2S_HandleTypeDef *hi2s;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_I2S_Receive_DMA(hi2s, (uint16_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return ros_cube_status(status);
}

int hal_i2s_abort(hal_i2s_t *handle)
{
    I2S_HandleTypeDef *hi2s;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    (void)HAL_I2S_DeInit(hi2s);
    handle->base.state = HAL_STATE_READY;
    return ros_cube_status(HAL_I2S_Init(hi2s));
}


#endif /* ROS_STM32_F4 */
