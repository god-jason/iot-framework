#include "iot_sensor_ntc.h"
#include "iot_mcu.h"
#include "iot_logger.h"

#include <math.h>

int iot_sensor_ntc_init(iot_sensor_ntc_t *s, uint8_t channel,
                        uint32_t r_series, float r25, float beta,
                        uint16_t adc_max, uint16_t vref_mv)
{
    if (s == NULL || r25 <= 0.0f || beta <= 0.0f) {
        return IOT_ERR_PARAM;
    }
    s->adc_channel = channel;
    s->adc_max = adc_max;
    s->vref_mv = vref_mv;
    s->r_series = r_series;
    s->r25 = r25;
    s->beta = beta;
    return iot_mcu_adc_init(channel);
}

int iot_sensor_ntc_read(iot_sensor_ntc_t *s, int16_t *temp_dc)
{
    uint16_t raw;
    float v, r, t_k;

    if (s == NULL || temp_dc == NULL) {
        return IOT_ERR_PARAM;
    }
    if (iot_mcu_adc_read(s->adc_channel, &raw) != IOT_OK) {
        LOG_E("ntc", "adc read fail");   /* adc read failed */
        return IOT_ERR_IO;
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
    return IOT_OK;
}