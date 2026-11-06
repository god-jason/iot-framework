#ifndef IOT_SENSOR_OPT3001_H
#define IOT_SENSOR_OPT3001_H

// OPT3001 数字环境光传感器（I2C 0x44 或 0x45，自动量程）
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：上电连续转换 + 自动量程
int iot_opt3001_init(const iot_i2c_bus_t *bus);
// 读取光照强度，单位 lux
int iot_opt3001_read(const iot_i2c_bus_t *bus, float *lux);

#ifdef __cplusplus
}
#endif

#endif