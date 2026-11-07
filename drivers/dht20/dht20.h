#ifndef DHT20_H
#define DHT20_H

// DHT20 温湿度传感器（I2C 0x38，协议同 AHT20）。
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（软复位 + 校准） */
int dht20_init(const i2c_bus_t *bus);

/* 触发测量并读取温度(℃)与湿度(%RH) */
int dht20_read(const i2c_bus_t *bus, float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* DHT20_H */