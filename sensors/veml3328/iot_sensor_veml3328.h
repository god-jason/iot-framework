#ifndef IOT_SENSOR_VEML3328_H
#define IOT_SENSOR_VEML3328_H

// VEML3328 彩色传感器（I2C 0x10，RGB + IR）
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：使能传感器
int iot_veml3328_init(const iot_i2c_bus_t *bus);
// 读取 R/G/B/IR 四通道原始值（16 位）
int iot_veml3328_read(const iot_i2c_bus_t *bus,
                      uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *ir);

#ifdef __cplusplus
}
#endif

#endif