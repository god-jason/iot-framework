#ifndef VOLTAGE_H
#define VOLTAGE_H

/*
 * 电压传感器：ADC 采样 -> 均值滤波 -> 换算电压(mV)。
 */

#include "iot_types.h"
#include "filter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t adc_channel;
    uint16_t adc_max;     /* ADC 满量程（如 4095，12bit） */
    uint16_t vref_mv;     /* 参考电压 mV */
    filter_mean_t filter;
    float fbuf[8];
} voltage_t;

int voltage_init(voltage_t *s, uint8_t channel,
                            uint16_t adc_max, uint16_t vref_mv);

/* 返回电压 mV（已滤波） */
int voltage_read(voltage_t *s, uint16_t *mv);

#ifdef __cplusplus
}
#endif

#endif /* VOLTAGE_H */