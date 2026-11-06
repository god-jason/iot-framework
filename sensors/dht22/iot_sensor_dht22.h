#ifndef IOT_SENSOR_DHT22_H
#define IOT_SENSOR_DHT22_H

/* DHT22 温湿度：湿度 16bit 无符号/10，温度 16bit 有符号/10（bit15 为符号）。 */
#include "bus/iot_bus_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_dht22_init(const iot_gpio_bus_t *bus, uint8_t pin);

/* 读取温度（摄氏度，0.1 分辨率）与湿度（%RH，0.1 分辨率），失败返回负值 */
int iot_dht22_read(const iot_gpio_bus_t *bus, uint8_t pin, float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_DHT22_H */