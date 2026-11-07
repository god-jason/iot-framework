#ifndef VL6180X_H
#define VL6180X_H

// VL6180X ToF 测距/接近传感器（I2C 0x29，单字节寄存器地址）
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：探测器件（完整初始化序列见 .c 说明）
int vl6180x_init(const i2c_bus_t *bus);
// 读取距离，单位 mm
int vl6180x_read(const i2c_bus_t *bus, uint16_t *distance_mm);

#ifdef __cplusplus
}
#endif

#endif