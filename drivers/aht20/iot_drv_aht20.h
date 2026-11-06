#ifndef IOT_DRV_AHT20_H
#define IOT_DRV_AHT20_H

/* AHT20（兼容 DHT20）I2C 温湿度传感器（I2C 0x38）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（软复位 + 校准） */
int iot_aht20_init(const iot_i2c_bus_t *bus);

/* 触发一次测量（后续需 ≥80ms 延时可读取） */
int iot_aht20_trigger(const iot_i2c_bus_t *bus);

/* 读取温度(℃)与湿度(%RH) */
int iot_aht20_read(const iot_i2c_bus_t *bus, float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif