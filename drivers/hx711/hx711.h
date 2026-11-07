#ifndef HX711_H
#define HX711_H

/* HX711 24 位称重 ADC（2 线 GPIO：SCK + DOUT，软件时序）。 */
#include "bus/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：拉低 SCK */
int hx711_init(const gpio_bus_t *gpio, uint8_t sck, uint8_t dout);

/* 读取一次原始重量值（24 位补码，扩展为有符号 int32） */
int hx711_read(const gpio_bus_t *gpio, uint8_t sck, uint8_t dout, int32_t *value);

#ifdef __cplusplus
}
#endif

#endif /* HX711_H */