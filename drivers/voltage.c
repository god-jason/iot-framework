#include "voltage.h"
#include "hal.h"
#include "log.h"

#define VOLTAGE_ADC_CHANNEL_MAX 16

static hal_adc_t s_adc_handles[VOLTAGE_ADC_CHANNEL_MAX];
static bool s_adc_ready[VOLTAGE_ADC_CHANNEL_MAX];

int voltage_init(voltage_t *s, uint8_t channel,
                            uint16_t adc_max, uint16_t vref_mv)
{
    hal_adc_config_t cfg;

    if (s == NULL || adc_max == 0 || channel >= VOLTAGE_ADC_CHANNEL_MAX) {
        return -1;
    }
    s->adc_channel = channel;
    s->adc_max = adc_max;
    s->vref_mv = vref_mv;
    filter_mean_init(&s->filter, s->fbuf, 8);

    if (!s_adc_ready[channel]) {
        cfg.resolution = HAL_ADC_RESOLUTION_12BIT;
        cfg.alignment = HAL_ADC_ALIGN_RIGHT;
        cfg.trigger = HAL_ADC_TRIGGER_SOFTWARE;
        cfg.channel_count = 1;
        if (hal_adc_init(&s_adc_handles[channel], &cfg) != HAL_OK) {
            return -1;
        }
        s_adc_ready[channel] = true;
    }
    return HAL_OK;
}

int voltage_read(voltage_t *s, uint16_t *mv)
{
    uint32_t raw;
    float f;

    if (s == NULL || mv == NULL ||
        s->adc_channel >= VOLTAGE_ADC_CHANNEL_MAX ||
        !s_adc_ready[s->adc_channel]) {
        return -1;
    }
    if (hal_adc_read(&s_adc_handles[s->adc_channel], s->adc_channel,
                      &raw, 0) != HAL_OK) {
        LOG_E("volt", "adc read fail");   /* adc read failed */
        return -1;
    }
    f = filter_mean_update(&s->filter, (float)raw);
    *mv = (uint16_t)(f / (float)s->adc_max * (float)s->vref_mv);
    return HAL_OK;
}
