/*
 * hal_adc.c - STM32F4 ADC HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 *
 * 编号：config.id = 0..N 依次对应 ADC1/ADC2/ADC3；
 * hal_adc_init() 内部按编号取静态 Cube 句柄，调用者无需提供实例。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

#define STM32_ADC_COUNT    3U

static ADC_HandleTypeDef s_hadc[STM32_ADC_COUNT];

static ADC_TypeDef *const s_adc_inst[STM32_ADC_COUNT] = {
    [0] = ADC1,
#ifdef ADC2
    [1] = ADC2,
#endif
#ifdef ADC3
    [2] = ADC3,
#endif
};

static ADC_HandleTypeDef *stm32_adc_instance(uint8_t instance)
{
    ADC_HandleTypeDef *hadc;

    if ((instance >= STM32_ADC_COUNT) || (s_adc_inst[instance] == NULL)) {
        return NULL;
    }
    hadc = &s_hadc[instance];
    memset(hadc, 0, sizeof(*hadc));
    hadc->Instance = s_adc_inst[instance];
    return hadc;
}

int hal_adc_init(hal_adc_t *handle,
                            const hal_adc_config_t *config)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    hadc = stm32_adc_instance(config->id);
    if (hadc == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->base.instance = hadc;

    hadc->Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    switch (config->resolution) {
    case HAL_ADC_RESOLUTION_6BIT:
        hadc->Init.Resolution = ADC_RESOLUTION_6B;
        break;
    case HAL_ADC_RESOLUTION_8BIT:
        hadc->Init.Resolution = ADC_RESOLUTION_8B;
        break;
    case HAL_ADC_RESOLUTION_10BIT:
        hadc->Init.Resolution = ADC_RESOLUTION_10B;
        break;
    case HAL_ADC_RESOLUTION_12BIT:
        hadc->Init.Resolution = ADC_RESOLUTION_12B;
        break;
    default:
        return HAL_NOT_SUPPORTED;
    }
    hadc->Init.DataAlign = (config->alignment == HAL_ADC_ALIGN_LEFT) ?
                           ADC_DATAALIGN_LEFT : ADC_DATAALIGN_RIGHT;
    hadc->Init.ScanConvMode = (config->channel_count > 1U) ? ENABLE : DISABLE;
    hadc->Init.ContinuousConvMode = DISABLE;
    hadc->Init.DiscontinuousConvMode = DISABLE;
    hadc->Init.NbrOfDiscConversion = 0U;
    hadc->Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc->Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc->Init.DMAContinuousRequests = DISABLE;
    hadc->Init.EOCSelection = ADC_EOC_SINGLE_CONV;

    if (HAL_ADC_Init(hadc) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_adc_deinit(hal_adc_t *handle)
{
    ADC_HandleTypeDef *hadc;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc != NULL) {
        (void)HAL_ADC_DeInit(hadc);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_adc_config_channel(hal_adc_t *handle,
                                      const hal_adc_channel_config_t *config)
{
    ADC_HandleTypeDef *hadc;
    ADC_ChannelConfTypeDef channel_config;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if ((config->rank == 0U) || (config->rank > 16U)) {
        return HAL_INVALID_PARAM;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }

    memset(&channel_config, 0, sizeof(channel_config));
    channel_config.Channel = config->channel;
    channel_config.Rank = config->rank;
    channel_config.SamplingTime = (config->sample_time == 0U) ?
                                  ADC_SAMPLETIME_56CYCLES :
                                  config->sample_time;
    return ros_cube_status(HAL_ADC_ConfigChannel(hadc, &channel_config));
}

int hal_adc_start(hal_adc_t *handle)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(HAL_ADC_Start(hadc));
}

int hal_adc_stop(hal_adc_t *handle)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(HAL_ADC_Stop(hadc));
}

int hal_adc_read(hal_adc_t *handle, uint8_t channel,
                            uint32_t *value, uint32_t timeout)
{
    ADC_HandleTypeDef *hadc;
    ADC_ChannelConfTypeDef channel_config;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (value == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }

    memset(&channel_config, 0, sizeof(channel_config));
    channel_config.Channel = channel;
    channel_config.Rank = 1U;
    channel_config.SamplingTime = ADC_SAMPLETIME_56CYCLES;

    status = HAL_ADC_ConfigChannel(hadc, &channel_config);
    if (status != HAL_OK) {
        return ros_cube_status(status);
    }

    status = HAL_ADC_Start(hadc);
    if (status != HAL_OK) {
        return ros_cube_status(status);
    }

    status = HAL_ADC_PollForConversion(hadc, timeout);
    if (status == HAL_OK) {
        *value = HAL_ADC_GetValue(hadc);
    }
    (void)HAL_ADC_Stop(hadc);
    return ros_cube_status(status);
}

int hal_adc_start_it(hal_adc_t *handle)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(HAL_ADC_Start_IT(hadc));
}

int hal_adc_start_dma(hal_adc_t *handle,
                                 uint32_t *buffer, size_t length)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (buffer == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(HAL_ADC_Start_DMA(hadc, buffer, (uint32_t)length));
}

int hal_adc_calibrate(hal_adc_t *handle)
{
    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    /* STM32F4 的 ADC 没有硬件自校准，保持跨平台 API 一致返回不支持 */
    return HAL_NOT_SUPPORTED;
}

uint32_t hal_adc_get_max_value(hal_adc_t *handle)
{
    uint32_t resolution;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return 0U;
    }
    resolution = (uint32_t)handle->config.resolution;
    if ((resolution == 0U) || (resolution >= 32U)) {
        return 0xFFFFFFFFU;
    }
    return (1UL << resolution) - 1U;
}


#endif /* ROS_STM32_F4 */
