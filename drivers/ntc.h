#ifndef NTC_H
#define NTC_H

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
} ntc_t;

int ntc_init(ntc_t *s, uint8_t channel,
                        uint32_t r_series, float r25, float beta,
                        uint16_t adc_max, uint16_t vref_mv);

/* 返回温度（0.1℃ 单位，如 253 = 25.3℃） */
int ntc_read(ntc_t *s, int16_t *temp_dc);

#ifdef __cplusplus
}
#endif

#endif /* NTC_H */