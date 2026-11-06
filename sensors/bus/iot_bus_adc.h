#ifndef IOT_BUS_ADC_H
#define IOT_BUS_ADC_H

/* 通用 ADC 采样总线：模拟量传感器（MQ、光敏、土壤、电流、挠曲等）统一从此读电压。 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*read_mv)(void *ctx, uint16_t *mv);   /* 读电压，单位 mV */
    void *ctx;
} iot_adc_bus_t;

int iot_adc_read_mv(const iot_adc_bus_t *bus, uint16_t *mv);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BUS_ADC_H */