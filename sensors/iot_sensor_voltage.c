#include "iot_sensor_voltage.h"
#include "iot_mcu.h"
#include "iot_logger.h"

int iot_sensor_voltage_init(iot_sensor_voltage_t *s, uint8_t channel,
                            uint16_t adc_max, uint16_t vref_mv)
{
    if (s == NULL || adc_max == 0) {
        return IOT_ERR_PARAM;
    }
    s->adc_channel = channel;
    s->adc_max = adc_max;
    s->vref_mv = vref_mv;
    iot_filter_mean_init(&s->filter, s->fbuf, 8);
    return iot_mcu_adc_init(channel);
}

int iot_sensor_voltage_read(iot_sensor_voltage_t *s, uint16_t *mv)
{
    uint16_t raw;
    float f;

    if (s == NULL || mv == NULL) {
        return IOT_ERR_PARAM;
    }
    if (iot_mcu_adc_read(s->adc_channel, &raw) != IOT_OK) {
        LOG_E("volt", "adc read fail");   /* adc read failed */
        return IOT_ERR_IO;
    }
    f = iot_filter_mean_update(&s->filter, (float)raw);
    *mv = (uint16_t)(f / (float)s->adc_max * (float)s->vref_mv);
    return IOT_OK;
}