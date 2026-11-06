#ifndef IOT_SENSOR_VOLTAGE_H
#define IOT_SENSOR_VOLTAGE_H

/*
 * 电压传感器：ADC 采样 -> 均值滤波 -> 换算电压(mV)。
 */

#include "iot_types.h"
#include "iot_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t adc_channel;
    uint16_t adc_max;     /* ADC 满量程（如 4095，12bit） */
    uint16_t vref_mv;     /* 参考电压 mV */
    iot_filter_mean_t filter;
    float fbuf[8];
} iot_sensor_voltage_t;

int iot_sensor_voltage_init(iot_sensor_voltage_t *s, uint8_t channel,
                            uint16_t adc_max, uint16_t vref_mv);

/* 返回电压 mV（已滤波） */
int iot_sensor_voltage_read(iot_sensor_voltage_t *s, uint16_t *mv);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_VOLTAGE_H */