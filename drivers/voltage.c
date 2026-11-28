#include "voltage.h"
#include "hal.h"
#include "ros.h"
#include "common.h"

/* 所有通道共用同一个 ADC 外设（编号 0），按 channel 切换规则组。 */
static hal_adc_t s_adc;
static bool s_adc_ready;

int voltage_init(voltage_t *s, uint8_t channel,
                            uint16_t adc_max, uint16_t vref_mv)
{
    hal_adc_config_t cfg;

    if (s == NULL || adc_max == 0) {
        return -1;
    }
    s->adc_channel = channel;
    s->adc_max = adc_max;
    s->vref_mv = vref_mv;
    filter_mean_init(&s->filter, s->fbuf, 8);

    if (!s_adc_ready) {
        cfg.resolution = HAL_ADC_RESOLUTION_12BIT;
        cfg.alignment = HAL_ADC_ALIGN_RIGHT;
        cfg.trigger = HAL_ADC_TRIGGER_SOFTWARE;
        cfg.channel_count = 1;
        cfg.id = 0U;
        if (hal_adc_init(&s_adc, &cfg) != HAL_OK) {
            return -1;
        }
        s_adc_ready = true;
    }
    return HAL_OK;
}

int voltage_read(voltage_t *s, uint16_t *mv)
{
    uint32_t raw;
    float f;

    if (s == NULL || mv == NULL || !s_adc_ready) {
        return -1;
    }
    /* timeout=0 的语义是"不等待"，单次转换必然取不到结果 */
    if (hal_adc_read(&s_adc, s->adc_channel, &raw, HAL_ADC_TIMEOUT_DEFAULT) != HAL_OK) {
        LOG_E("volt", "adc read fail");   /* adc read failed */
        return -1;
    }
    f = filter_mean_update(&s->filter, (float)raw);
    *mv = (uint16_t)(f / (float)s->adc_max * (float)s->vref_mv);
    return HAL_OK;
}
