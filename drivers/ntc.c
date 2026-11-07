#include "ntc.h"
#include "port.h"
#include "iot_logger.h"

#include <math.h>

#define NTC_ADC_CHANNEL_MAX 16

static port_adc_handle_t s_adc_handles[NTC_ADC_CHANNEL_MAX];
static bool s_adc_ready[NTC_ADC_CHANNEL_MAX];

int ntc_init(ntc_t *s, uint8_t channel,
                        uint32_t r_series, float r25, float beta,
                        uint16_t adc_max, uint16_t vref_mv)
{
    port_adc_config_t cfg;

    if (s == NULL || r25 <= 0.0f || beta <= 0.0f ||
        channel >= NTC_ADC_CHANNEL_MAX) {
        return -1;
    }
    s->adc_channel = channel;
    s->adc_max = adc_max;
    s->vref_mv = vref_mv;
    s->r_series = r_series;
    s->r25 = r25;
    s->beta = beta;

    if (!s_adc_ready[channel]) {
        cfg.resolution = PORT_ADC_RESOLUTION_12BIT;
        cfg.alignment = PORT_ADC_ALIGN_RIGHT;
        cfg.trigger = PORT_ADC_TRIGGER_SOFTWARE;
        cfg.channel_count = 1;
        if (port_adc_init(&s_adc_handles[channel], &cfg) != PORT_OK) {
            return -1;
        }
        s_adc_ready[channel] = true;
    }
    return PORT_OK;
}

int ntc_read(ntc_t *s, int16_t *temp_dc)
{
    uint32_t raw;
    float v, r, t_k;

    if (s == NULL || temp_dc == NULL ||
        s->adc_channel >= NTC_ADC_CHANNEL_MAX ||
        !s_adc_ready[s->adc_channel]) {
        return -1;
    }
    if (port_adc_read(&s_adc_handles[s->adc_channel], s->adc_channel,
                      &raw, 0) != PORT_OK) {
        LOG_E("ntc", "adc read fail");   /* adc read failed */
        return -1;
    }
    v = (float)raw / (float)s->adc_max * (float)s->vref_mv;
    if (v >= (float)s->vref_mv) {
        r = 1e7f;   /* 开路：极大电阻 */
    } else {
        r = (float)s->r_series * v / ((float)s->vref_mv - v);
    }
    /* B 值公式：1/T = 1/T25 + ln(R/R25)/B */
    t_k = 1.0f / (1.0f / 298.15f + logf(r / s->r25) / s->beta);
    *temp_dc = (int16_t)((t_k - 273.15f) * 10.0f);
    return PORT_OK;
}
