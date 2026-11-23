/*
 * hal_adc.c - STM32F1 ADC 寄存器实现（不依赖 Cube HAL）
 *
 * 仅支持 12 位分辨率，ADC1/ADC2 规则组软件触发轮询与 EOC 中断。
 * DMA 方式在 F1 平台未实现，返回 HAL_NOT_SUPPORTED。
 * 采样时间参数按 ADC 时钟周期数解释（1.5/7.5/13.5/28.5/41.5/55.5/
 * 71.5/239.5 取最接近档位）。
 *
 * 编号：config.id = 0/1 依次对应 ADC1/ADC2（按芯片裁剪）。
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "stm32f10x.h"

#define STM32_ADC_CR2_ADON      (1UL << 0)
#define STM32_ADC_CR2_CONT      (1UL << 1)
#define STM32_ADC_CR2_CAL       (1UL << 2)
#define STM32_ADC_CR2_RSTCAL    (1UL << 3)
#define STM32_ADC_CR2_ALIGN     (1UL << 11)
#define STM32_ADC_CR2_EXTSEL_SHIFT 17U
#define STM32_ADC_CR2_EXTTRIG   (1UL << 20)
#define STM32_ADC_CR2_SWSTART   (1UL << 22)

#define STM32_ADC_SR_EOC        (1UL << 1)
#define STM32_ADC_SR_EOCIE      (1UL << 5)

#define STM32_ADC_CR2_EXTSEL_SWSTART  7UL
#define STM32_ADC_CR2_EXTSEL_TIM1_CC1 4UL
#define STM32_ADC_CR2_EXTSEL_EXTI11   6UL

static hal_adc_t *s_stm32_adc[2];

static ADC_TypeDef *const s_stm32_adc_inst[] = {
    ADC1,
#ifdef ADC2
    ADC2,
#endif
};

#define STM32_ADC_CH_COUNT \
    (sizeof(s_stm32_adc_inst) / sizeof(s_stm32_adc_inst[0]))

static uint8_t stm32_adc_index(ADC_TypeDef *adc)
{
    if (adc == ADC1) {
        return 0U;
    }
#ifdef ADC2
    if (adc == ADC2) {
        return 1U;
    }
#endif
    return 0xFFU;
}

static uint32_t stm32_adc_smp_code(uint32_t cycles)
{
    if (cycles <= 1U) {
        return 0U;      /* 1.5 */
    }
    if (cycles <= 7U) {
        return 1U;      /* 7.5 */
    }
    if (cycles <= 13U) {
        return 2U;      /* 13.5 */
    }
    if (cycles <= 28U) {
        return 3U;      /* 28.5 */
    }
    if (cycles <= 41U) {
        return 4U;      /* 41.5 */
    }
    if (cycles <= 55U) {
        return 5U;      /* 55.5 */
    }
    if (cycles <= 71U) {
        return 6U;      /* 71.5 */
    }
    return 7U;          /* 239.5 */
}

static void stm32_adc_set_sample_time(ADC_TypeDef *adc, uint8_t channel,
                                      uint32_t cycles)
{
    uint32_t code = stm32_adc_smp_code(cycles);

    if (channel <= 9U) {
        uint32_t shift = (uint32_t)channel * 3U;

        adc->SMPR2 = (adc->SMPR2 & ~(0x7UL << shift)) | (code << shift);
    } else if (channel <= 17U) {
        uint32_t shift = ((uint32_t)channel - 10U) * 3U;

        adc->SMPR1 = (adc->SMPR1 & ~(0x7UL << shift)) | (code << shift);
    }
}

static void stm32_adc_set_rank(ADC_TypeDef *adc, uint8_t rank,
                               uint8_t channel)
{
    if (rank <= 6U) {
        uint32_t shift = ((uint32_t)rank - 1U) * 5U;

        adc->SQR3 = (adc->SQR3 & ~(0x1FUL << shift)) | (channel << shift);
    } else if (rank <= 12U) {
        uint32_t shift = ((uint32_t)rank - 7U) * 5U;

        adc->SQR2 = (adc->SQR2 & ~(0x1FUL << shift)) | (channel << shift);
    } else {
        uint32_t shift = ((uint32_t)rank - 13U) * 5U;

        adc->SQR1 = (adc->SQR1 & ~(0x1FUL << shift)) | (channel << shift);
    }
}

static void stm32_adc_set_count(ADC_TypeDef *adc, uint8_t count)
{
    uint32_t l = (count == 0U) ? 0U : (uint32_t)(count - 1U);

    adc->SQR1 = (adc->SQR1 & ~(0xFUL << 20)) | ((l & 0xFUL) << 20);
}

static void stm32_adc_clock_enable(ADC_TypeDef *adc)
{
    /* ADC 时钟 = PCLK2 / 6（72MHz -> 12MHz，满足 <=14MHz） */
    RCC->CFGR = (RCC->CFGR & ~(0x3UL << 14)) | (0x2UL << 14);
    if (adc == ADC1) {
        RCC->APB2ENR |= (1UL << 9);
    }
#ifdef ADC2
    else if (adc == ADC2) {
        RCC->APB2ENR |= (1UL << 10);
    }
#endif
}

int hal_adc_init(hal_adc_t *handle, const hal_adc_config_t *config)
{
    ADC_TypeDef *adc;
    volatile uint32_t settle;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (config->resolution != HAL_ADC_RESOLUTION_12BIT) {
        return HAL_NOT_SUPPORTED;
    }

    if (config->id >= STM32_ADC_CH_COUNT) {
        return HAL_INVALID_PARAM;
    }
    adc = s_stm32_adc_inst[config->id];
    handle->base.instance = adc;

    stm32_adc_clock_enable(adc);

    adc->CR1 = 0U;
    adc->CR2 = STM32_ADC_CR2_ADON;
    if (config->alignment == HAL_ADC_ALIGN_LEFT) {
        adc->CR2 |= STM32_ADC_CR2_ALIGN;
    }

    switch (config->trigger) {
    case HAL_ADC_TRIGGER_SOFTWARE:
        adc->CR2 |= (STM32_ADC_CR2_EXTSEL_SWSTART << STM32_ADC_CR2_EXTSEL_SHIFT) |
                    STM32_ADC_CR2_EXTTRIG;
        break;
    case HAL_ADC_TRIGGER_TIMER:
        adc->CR2 |= (STM32_ADC_CR2_EXTSEL_TIM1_CC1 << STM32_ADC_CR2_EXTSEL_SHIFT) |
                    STM32_ADC_CR2_EXTTRIG;
        break;
    case HAL_ADC_TRIGGER_EXTERNAL:
        adc->CR2 |= (STM32_ADC_CR2_EXTSEL_EXTI11 << STM32_ADC_CR2_EXTSEL_SHIFT) |
                    STM32_ADC_CR2_EXTTRIG;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    /* 上电稳定（tSTAB）后执行复位校准 */
    for (settle = 0U; settle < 1000U; settle++) {
        ;
    }
    adc->CR2 |= STM32_ADC_CR2_RSTCAL;
    while ((adc->CR2 & STM32_ADC_CR2_RSTCAL) != 0U) {
        ;
    }
    adc->CR2 |= STM32_ADC_CR2_CAL;
    while ((adc->CR2 & STM32_ADC_CR2_CAL) != 0U) {
        ;
    }

    stm32_adc_set_count(adc, config->channel_count);
    adc->SR = 0U;

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_adc_deinit(hal_adc_t *handle)
{
    ADC_TypeDef *adc;
    uint8_t index;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    adc = (ADC_TypeDef *)handle->base.instance;
    if (adc != NULL) {
        adc->CR2 &= ~STM32_ADC_CR2_ADON;
        index = stm32_adc_index(adc);
        if (index < 2U) {
            s_stm32_adc[index] = NULL;
        }
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_adc_config_channel(hal_adc_t *handle,
                           const hal_adc_channel_config_t *config)
{
    ADC_TypeDef *adc;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if ((config->rank == 0U) || (config->rank > 16U)) {
        return HAL_INVALID_PARAM;
    }
    adc = (ADC_TypeDef *)handle->base.instance;
    if (adc == NULL) {
        return HAL_NOT_READY;
    }

    stm32_adc_set_sample_time(adc, config->channel, config->sample_time);
    stm32_adc_set_rank(adc, config->rank, config->channel);
    if (config->rank > handle->config.channel_count) {
        handle->config.channel_count = config->rank;
        stm32_adc_set_count(adc, config->rank);
    }
    return HAL_OK;
}

int hal_adc_start(hal_adc_t *handle)
{
    ADC_TypeDef *adc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    adc = (ADC_TypeDef *)handle->base.instance;
    if (adc == NULL) {
        return HAL_NOT_READY;
    }

    adc->CR2 |= STM32_ADC_CR2_CONT;
    adc->CR2 |= STM32_ADC_CR2_SWSTART;
    return HAL_OK;
}

int hal_adc_stop(hal_adc_t *handle)
{
    ADC_TypeDef *adc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    adc = (ADC_TypeDef *)handle->base.instance;
    if (adc == NULL) {
        return HAL_NOT_READY;
    }

    adc->CR2 &= ~STM32_ADC_CR2_CONT;
    return HAL_OK;
}

int hal_adc_read(hal_adc_t *handle, uint8_t channel,
                 uint32_t *value, uint32_t timeout)
{
    ADC_TypeDef *adc;
    uint32_t start;

    if ((handle == NULL) || (value == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    adc = (ADC_TypeDef *)handle->base.instance;
    if (adc == NULL) {
        return HAL_NOT_READY;
    }

    stm32_adc_set_sample_time(adc, channel, 55U);
    stm32_adc_set_rank(adc, 1U, channel);
    stm32_adc_set_count(adc, 1U);

    adc->SR = 0U;
    adc->CR2 &= ~STM32_ADC_CR2_CONT;
    adc->CR2 |= STM32_ADC_CR2_SWSTART;

    start = hal_tick_get();
    while ((adc->SR & STM32_ADC_SR_EOC) == 0U) {
        if (timeout != HAL_TIMEOUT_FOREVER &&
            ((uint32_t)(hal_tick_get() - start) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }
    *value = adc->DR & 0xFFFFU;
    return HAL_OK;
}

int hal_adc_start_it(hal_adc_t *handle)
{
    ADC_TypeDef *adc;
    uint8_t index;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    adc = (ADC_TypeDef *)handle->base.instance;
    if (adc == NULL) {
        return HAL_NOT_READY;
    }
    index = stm32_adc_index(adc);
    if (index >= 2U) {
        return HAL_NOT_SUPPORTED;
    }

    s_stm32_adc[index] = handle;
    adc->CR1 |= STM32_ADC_SR_EOCIE;
    NVIC_EnableIRQ(ADC1_2_IRQn);
    adc->CR2 |= STM32_ADC_CR2_SWSTART;
    return HAL_OK;
}

int hal_adc_start_dma(hal_adc_t *handle, uint32_t *buffer, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(buffer);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_adc_calibrate(hal_adc_t *handle)
{
    ADC_TypeDef *adc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    adc = (ADC_TypeDef *)handle->base.instance;
    if (adc == NULL) {
        return HAL_NOT_READY;
    }

    adc->CR2 |= STM32_ADC_CR2_RSTCAL;
    while ((adc->CR2 & STM32_ADC_CR2_RSTCAL) != 0U) {
        ;
    }
    adc->CR2 |= STM32_ADC_CR2_CAL;
    while ((adc->CR2 & STM32_ADC_CR2_CAL) != 0U) {
        ;
    }
    return HAL_OK;
}

uint32_t hal_adc_get_max_value(hal_adc_t *handle)
{
    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return 0U;
    }
    return 4095U;
}

/* ADC1/ADC2 共用中断入口 */
void ADC1_2_IRQHandler(void)
{
    uint8_t i;

    for (i = 0U; i < 2U; i++) {
        hal_adc_t *handle = s_stm32_adc[i];
        ADC_TypeDef *adc = NULL;

        if (i == 0U) {
            adc = ADC1;
        }
#ifdef ADC2
        else {
            adc = ADC2;
        }
#endif
        if ((handle == NULL) || (adc == NULL)) {
            continue;
        }
        if ((adc->SR & STM32_ADC_SR_EOC) != 0U) {
            adc->SR = ~STM32_ADC_SR_EOC;    /* 写 0 清除 EOC */
            if (handle->callback != NULL) {
                handle->callback(handle, HAL_EVENT_COMPLETE,
                                 handle->user_data);
            }
        }
    }
}

#endif /* ROS_STM32_F1 */
