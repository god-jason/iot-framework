#ifndef AM2302_H
#define AM2302_H

/* AM2302 温湿度：与 DHT22 同款（湿度 16bit/10，温度 16bit 有符号/10）。 */
#include "bus/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int am2302_init(const gpio_bus_t *bus, uint8_t pin);

/* 读取温度（摄氏度）与湿度（%RH），失败返回负值 */
int am2302_read(const gpio_bus_t *bus, uint8_t pin, float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* AM2302_H */