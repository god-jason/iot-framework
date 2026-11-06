#ifndef IOT_SENSOR_NTC_H
#define IOT_SENSOR_NTC_H

/*
 * NTC 热敏电阻温度传感器。
 * 通过 ADC 测分压电压 -> 计算 NTC 电阻 -> 用 B 值公式换算温度(℃)。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t adc_channel;
    uint16_t adc_max;
    uint16_t vref_mv;
    uint32_t r_series;     /* 串联分压电阻(Ω) */
    float beta;            /* NTC B 值(K) */
    float r25;             /* 25℃ 时电阻(Ω) */
} iot_sensor_ntc_t;

int iot_sensor_ntc_init(iot_sensor_ntc_t *s, uint8_t channel,
                        uint32_t r_series, float r25, float beta,
                        uint16_t adc_max, uint16_t vref_mv);

/* 返回温度（0.1℃ 单位，如 253 = 25.3℃） */
int iot_sensor_ntc_read(iot_sensor_ntc_t *s, int16_t *temp_dc);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_NTC_H */