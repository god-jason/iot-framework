#ifndef APDS9960_H
#define APDS9960_H

// APDS9960 手势/接近/颜色传感器（I2C 0x39，颜色部分 RGBC）
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：使能 + 启动
int apds9960_init(const i2c_bus_t *bus);
// 读取 Clear/Red/Green/Blue 通道原始值（16 位）
int apds9960_read(const i2c_bus_t *bus,
                      uint16_t *c, uint16_t *r, uint16_t *g, uint16_t *b);

#ifdef __cplusplus
}
#endif

#endif