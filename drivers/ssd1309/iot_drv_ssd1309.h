#ifndef IOT_DRV_SSD1309_H
#define IOT_DRV_SSD1309_H

// SSD1309 128x64 单色 OLED 驱动（I2C 地址 0x3C，与 SSD1306 高度类似）。
// 仅提供初始化、整屏填充与清屏，不含字库与高级绘图，请按需扩展。
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_ssd1309_init(const iot_i2c_bus_t *bus);
int iot_ssd1309_fill(const iot_i2c_bus_t *bus, uint8_t pattern);
int iot_ssd1309_clear(const iot_i2c_bus_t *bus);

#ifdef __cplusplus
}
#endif

#endif