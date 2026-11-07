#ifndef DHT11_H
#define DHT11_H

/* DHT11 温湿度：专用单总线协议，GPIO bit-bang 实现。 */
#include "bus/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int dht11_init(const gpio_bus_t *bus, uint8_t pin);

/* 读取温度（摄氏度）与湿度（%RH），失败返回负值 */
int dht11_read(const gpio_bus_t *bus, uint8_t pin, float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* DHT11_H */