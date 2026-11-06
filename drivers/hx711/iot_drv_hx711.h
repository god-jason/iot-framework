#ifndef IOT_DRV_HX711_H
#define IOT_DRV_HX711_H

/* HX711 24 位称重 ADC（2 线 GPIO：SCK + DOUT，软件时序）。 */
#include "bus/iot_bus_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：拉低 SCK */
int iot_hx711_init(const iot_gpio_bus_t *gpio, uint8_t sck, uint8_t dout);

/* 读取一次原始重量值（24 位补码，扩展为有符号 int32） */
int iot_hx711_read(const iot_gpio_bus_t *gpio, uint8_t sck, uint8_t dout, int32_t *value);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_HX711_H */