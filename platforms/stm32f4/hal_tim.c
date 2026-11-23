/*
 * hal_tim.c - STM32F4 TIM / PWM / CAPTURE / ENCODER HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static uint32_t stm32_tim_counter_mode(hal_tim_counter_e mode)
{
    switch (mode) {
    case HAL_TIM_COUNTER_DOWN:
        return TIM_COUNTERMODE_DOWN;
    case HAL_TIM_COUNTER_CENTER:
        return TIM_COUNTERMODE_CENTERALIGNED1;
    case HAL_TIM_COUNTER_UP:
    default:
        return TIM_COUNTERMODE_UP;
    }
}

static uint32_t stm32_tim_clock_division(uint8_t division)
{
    switch (division) {
    case 2U:
        return TIM_CLOCKDIVISION_DIV2;
    case 4U:
        return TIM_CLOCKDIVISION_DIV4;
    case 1U:
    default:
        return TIM_CLOCKDIVISION_DIV1;
    }
}

static uint32_t stm32_tim_channel(uint32_t channel)
{
    switch (channel) {
    case 1U:
        return TIM_CHANNEL_1;
    case 2U:
        return TIM_CHANNEL_2;
    case 3U:
        return TIM_CHANNEL_3;
    case 4U:
        return TIM_CHANNEL_4;
    default:
        return 0U;
    }
}

static uint32_t stm32_tim_ic_prescaler(uint32_t prescaler)
{
    switch (prescaler) {
    case 2U:
        return TIM_ICPSC_DIV2;
    case 4U:
        return TIM_ICPSC_DIV4;
    case 8U:
        return TIM_ICPSC_DIV8;
    case 1U:
    default:
        return TIM_ICPSC_DIV1;
    }
}

int hal_tim_init(hal_tim_t *handle,
                            const hal_tim_config_t *config)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    memset(&htim->Init, 0, sizeof(htim->Init));
    htim->Init.Prescaler = config->prescaler;
    htim->Init.CounterMode = stm32_tim_counter_mode(config->counter_mode);
    htim->Init.Period = config->period;
    htim->Init.ClockDivision = stm32_tim_clock_division(config->clock_division);
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(htim) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_tim_deinit(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim != NULL) {
        (void)HAL_TIM_Base_DeInit(htim);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_tim_start(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    if (HAL_TIM_Base_Start(htim) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

int hal_tim_stop(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    (void)HAL_TIM_Base_Stop(htim);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_tim_start_it(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    if (HAL_TIM_Base_Start_IT(htim) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

int hal_tim_stop_it(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    (void)HAL_TIM_Base_Stop_IT(htim);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_tim_set_period(hal_tim_t *handle,
                                  uint32_t period)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }

    htim->Init.Period = period;
    __HAL_TIM_SET_AUTORELOAD(htim, period);
    handle->config.period = period;
    return HAL_OK;
}

int hal_tim_set_prescaler(hal_tim_t *handle,
                                     uint32_t prescaler)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }

    htim->Init.Prescaler = prescaler;
    __HAL_TIM_SET_PRESCALER(htim, prescaler);
    handle->config.prescaler = prescaler;
    return HAL_OK;
}

int hal_tim_set_counter(hal_tim_t *handle,
                                   uint32_t counter)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    __HAL_TIM_SET_COUNTER(htim, counter);
    return HAL_OK;
}

uint32_t hal_tim_get_counter(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return 0U;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    return (uint32_t)__HAL_TIM_GET_COUNTER(htim);
}

static uint32_t stm32_timer_clock(TIM_TypeDef *instance)
{
    uint32_t pclk;

#if defined(TIM8)
    if ((instance == TIM1) || (instance == TIM8)) {
        pclk = HAL_RCC_GetPCLK2Freq();
    } else {
        pclk = HAL_RCC_GetPCLK1Freq();
    }
#else
    if (instance == TIM1) {
        pclk = HAL_RCC_GetPCLK2Freq();
    } else {
        pclk = HAL_RCC_GetPCLK1Freq();
    }
#endif

    if (pclk != HAL_RCC_GetHCLKFreq()) {
        pclk *= 2U;
    }
    return pclk;
}

static void stm32_pwm_calculate(uint32_t timer_clock, uint32_t frequency,
                             uint32_t *prescaler, uint32_t *period)
{
    uint32_t psc = timer_clock / 1000000U;
    uint32_t per;

    if (psc == 0U) {
        psc = 1U;
    }
    if (psc > 65536U) {
        psc = 65536U;
    }

    per = (timer_clock / psc) / frequency;
    if (per == 0U) {
        per = 1U;
    }
    if (per > 65536U) {
        per = 65536U;
    }

    *prescaler = psc - 1U;
    *period = per - 1U;
}

static int stm32_pwm_config_output(hal_pwm_t *handle,
                                          TIM_HandleTypeDef *htim,
                                          uint32_t channel)
{
    TIM_OC_InitTypeDef oc;
    uint32_t duty = HAL_MIN(handle->config.duty, 10000U);
    uint32_t period = htim->Init.Period + 1U;

    memset(&oc, 0, sizeof(oc));
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = (period * duty) / 10000U;
    oc.OCPolarity = (handle->config.polarity == HAL_PWM_POLARITY_LOW) ?
                    TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;

    return ros_cube_status(HAL_TIM_PWM_ConfigChannel(htim, &oc, channel));
}

int hal_pwm_init(hal_pwm_t *handle,
                            const hal_pwm_config_t *config)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
    uint32_t prescaler;
    uint32_t period;

    if ((handle == NULL) || (config == NULL) || (config->frequency == 0U) ||
        (config->duty > 10000U)) {
        return HAL_INVALID_PARAM;
    }
    if ((handle->channel < 1U) || (handle->channel > 4U)) {
        handle->channel = 1U;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    channel = stm32_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    stm32_pwm_calculate(stm32_timer_clock(htim->Instance), config->frequency,
                     &prescaler, &period);

    memset(&htim->Init, 0, sizeof(htim->Init));
    htim->Init.Prescaler = prescaler;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = period;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_PWM_Init(htim) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    if (stm32_pwm_config_output(handle, htim, channel) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_pwm_deinit(hal_pwm_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim != NULL) {
        channel = stm32_tim_channel(handle->channel);
        if (channel != 0U) {
            (void)HAL_TIM_PWM_Stop(htim, channel);
        }
        (void)HAL_TIM_PWM_DeInit(htim);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_pwm_start(hal_pwm_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = stm32_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (HAL_TIM_PWM_Start(htim, channel) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

int hal_pwm_stop(hal_pwm_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = stm32_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    (void)HAL_TIM_PWM_Stop(htim, channel);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_pwm_set_frequency(hal_pwm_t *handle,
                                     uint32_t frequency)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
    uint32_t prescaler;
    uint32_t period;
    bool running;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET) ||
        (frequency == 0U)) {
        return HAL_INVALID_PARAM;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = stm32_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    running = (handle->base.state == HAL_STATE_BUSY);
    (void)HAL_TIM_PWM_Stop(htim, channel);

    stm32_pwm_calculate(stm32_timer_clock(htim->Instance), frequency,
                     &prescaler, &period);
    htim->Init.Prescaler = prescaler;
    htim->Init.Period = period;
    if (HAL_TIM_PWM_Init(htim) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config.frequency = frequency;
    if (stm32_pwm_config_output(handle, htim, channel) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    if (running) {
        (void)HAL_TIM_PWM_Start(htim, channel);
        handle->base.state = HAL_STATE_BUSY;
    } else {
        handle->base.state = HAL_STATE_READY;
    }
    return HAL_OK;
}

int hal_pwm_set_duty(hal_pwm_t *handle, uint32_t duty)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
    uint32_t compare;
    uint32_t period;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    if (duty > 10000U) {
        duty = 10000U;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = stm32_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    period = htim->Init.Period + 1U;
    compare = (period * duty) / 10000U;
    __HAL_TIM_SET_COMPARE(htim, channel, compare);
    handle->config.duty = duty;
    return HAL_OK;
}

int hal_pwm_set_polarity(hal_pwm_t *handle,
                                    hal_pwm_polarity_e polarity)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = stm32_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    handle->config.polarity = polarity;
    return stm32_pwm_config_output(handle, htim, channel);
}

static uint32_t stm32_capture_channel(hal_capture_t *handle)
{
    if (handle == NULL) {
        return 0U;
    }
    return stm32_tim_channel(1U);
}

int hal_capture_init(hal_capture_t *handle,
                                const hal_capture_config_t *config)
{
    TIM_HandleTypeDef *htim;
    TIM_IC_InitTypeDef ic;
    uint32_t channel;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_NOT_READY;
    }
    channel = stm32_capture_channel(handle);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    memset(&htim->Init, 0, sizeof(htim->Init));
    htim->Init.Prescaler = 0U;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = 0xFFFFU;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_IC_Init(htim) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    memset(&ic, 0, sizeof(ic));
    switch (config->edge) {
    case HAL_CAPTURE_FALLING:
        ic.ICPolarity = TIM_ICPOLARITY_FALLING;
        break;
    case HAL_CAPTURE_BOTH:
        ic.ICPolarity = TIM_ICPOLARITY_BOTHEDGE;
        break;
    case HAL_CAPTURE_RISING:
    default:
        ic.ICPolarity = TIM_ICPOLARITY_RISING;
        break;
    }
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = stm32_tim_ic_prescaler(config->prescaler);
    ic.ICFilter = (uint32_t)(config->filter & 0x0FU);

    if (HAL_TIM_IC_ConfigChannel(htim, &ic, channel) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_capture_start(hal_capture_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = stm32_capture_channel(handle);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (HAL_TIM_IC_Start(htim, channel) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

int hal_capture_stop(hal_capture_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = stm32_capture_channel(handle);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    (void)HAL_TIM_IC_Stop_IT(htim, channel);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_capture_start_it(hal_capture_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = stm32_capture_channel(handle);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (HAL_TIM_IC_Start_IT(htim, channel) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

uint32_t hal_capture_get_value(hal_capture_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return 0U;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    channel = stm32_capture_channel(handle);
    if (channel == 0U) {
        return 0U;
    }
    return HAL_TIM_ReadCapturedValue(htim, channel);
}

int hal_encoder_init(hal_encoder_t *handle,
                                const hal_encoder_config_t *config)
{
    TIM_HandleTypeDef *htim;
    TIM_Encoder_InitTypeDef encoder;
    uint32_t polarity;
    uint32_t period;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    period = (config->period == 0U) ? 0xFFFFU : config->period;
    if (period > 0xFFFFU) {
        period = 0xFFFFU;
    }

    memset(&htim->Init, 0, sizeof(htim->Init));
    htim->Init.Prescaler = 0U;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = period;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    polarity = config->reverse ? TIM_ICPOLARITY_FALLING : TIM_ICPOLARITY_RISING;

    memset(&encoder, 0, sizeof(encoder));
    switch (config->mode) {
    case HAL_ENCODER_MODE_TI1:
        encoder.EncoderMode = TIM_ENCODERMODE_TI1;
        break;
    case HAL_ENCODER_MODE_TI2:
        encoder.EncoderMode = TIM_ENCODERMODE_TI2;
        break;
    case HAL_ENCODER_MODE_TI12:
    default:
        encoder.EncoderMode = TIM_ENCODERMODE_TI12;
        break;
    }
    encoder.IC1Polarity = polarity;
    encoder.IC1Prescaler = TIM_ICPSC_DIV1;
    encoder.IC1Filter = (uint32_t)(config->filter & 0x0FU);
    encoder.IC2Polarity = polarity;
    encoder.IC2Prescaler = TIM_ICPSC_DIV1;
    encoder.IC2Filter = (uint32_t)(config->filter & 0x0FU);

    if (HAL_TIM_Encoder_Init(htim, &encoder) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->config.period = period;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_encoder_start(hal_encoder_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    if (HAL_TIM_Encoder_Start(htim, TIM_CHANNEL_ALL) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

int hal_encoder_stop(hal_encoder_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    (void)HAL_TIM_Encoder_Stop(htim, TIM_CHANNEL_ALL);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int32_t hal_encoder_get_count(hal_encoder_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return 0;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    return (int32_t)__HAL_TIM_GET_COUNTER(htim);
}

int hal_encoder_set_count(hal_encoder_t *handle,
                                     int32_t count)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    __HAL_TIM_SET_COUNTER(htim, (uint32_t)count);
    return HAL_OK;
}


#endif /* ROS_STM32_F4 */
