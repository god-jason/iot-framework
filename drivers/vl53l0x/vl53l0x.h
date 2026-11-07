#ifndef VL53L0X_H
#define VL53L0X_H

// VL53L0X ToF 激光测距传感器（I2C 0x29，最远约 2m）
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：探测器件（完整测距初始化序列见 .c 说明）
int vl53l0x_init(const i2c_bus_t *bus);
// 读取距离，单位 mm
int vl53l0x_read(const i2c_bus_t *bus, uint16_t *distance_mm);

#ifdef __cplusplus
}
#endif

#endif