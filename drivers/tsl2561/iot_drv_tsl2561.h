#ifndef IOT_DRV_TSL2561_H
#define IOT_DRV_TSL2561_H

// TSL2561 环境光传感器（I2C 0x29/0x39，命令位 0x80）。
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（上电） */
int iot_tsl2561_init(const iot_i2c_bus_t *bus);

/* 读取环境光照度(lux) */
int iot_tsl2561_read(const iot_i2c_bus_t *bus, float *lux);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_TSL2561_H */