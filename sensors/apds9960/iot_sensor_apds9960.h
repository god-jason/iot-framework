#ifndef IOT_SENSOR_APDS9960_H
#define IOT_SENSOR_APDS9960_H

// APDS9960 手势/接近/颜色传感器（I2C 0x39，颜色部分 RGBC）
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：使能 + 启动
int iot_apds9960_init(const iot_i2c_bus_t *bus);
// 读取 Clear/Red/Green/Blue 通道原始值（16 位）
int iot_apds9960_read(const iot_i2c_bus_t *bus,
                      uint16_t *c, uint16_t *r, uint16_t *g, uint16_t *b);

#ifdef __cplusplus
}
#endif

#endif