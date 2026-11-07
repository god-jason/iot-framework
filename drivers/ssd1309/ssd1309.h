#ifndef SSD1309_H
#define SSD1309_H

// SSD1309 128x64 单色 OLED 驱动（I2C 地址 0x3C，与 SSD1306 高度类似）。
// 仅提供初始化、整屏填充与清屏，不含字库与高级绘图，请按需扩展。
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int ssd1309_init(const i2c_bus_t *bus);
int ssd1309_fill(const i2c_bus_t *bus, uint8_t pattern);
int ssd1309_clear(const i2c_bus_t *bus);

#ifdef __cplusplus
}
#endif

#endif