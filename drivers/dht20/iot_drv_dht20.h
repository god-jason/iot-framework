#ifndef IOT_DRV_DHT20_H
#define IOT_DRV_DHT20_H

// DHT20 温湿度传感器（I2C 0x38，协议同 AHT20）。
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（软复位 + 校准） */
int iot_dht20_init(const iot_i2c_bus_t *bus);

/* 触发测量并读取温度(℃)与湿度(%RH) */
int iot_dht20_read(const iot_i2c_bus_t *bus, float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_DHT20_H */