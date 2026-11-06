#ifndef IOT_SENSOR_TCS34725_H
#define IOT_SENSOR_TCS34725_H

// TCS34725 数字颜色传感器（I2C 0x29，RGBC 四通道）
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：使能 + 启动转换
int iot_tcs34725_init(const iot_i2c_bus_t *bus);
// 读取 Clear/Red/Green/Blue 通道原始值（16 位）
int iot_tcs34725_read(const iot_i2c_bus_t *bus,
                      uint16_t *c, uint16_t *r, uint16_t *g, uint16_t *b);

#ifdef __cplusplus
}
#endif

#endif