#ifndef IOT_SENSOR_VL53L1X_H
#define IOT_SENSOR_VL53L1X_H

// VL53L1X ToF 激光测距传感器（I2C 0x29，长测距约 4m，16 位寄存器地址）
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：探测器件（完整初始化序列见 .c 说明）
int iot_vl53l1x_init(const iot_i2c_bus_t *bus);
// 读取距离，单位 mm
int iot_vl53l1x_read(const iot_i2c_bus_t *bus, uint16_t *distance_mm);

#ifdef __cplusplus
}
#endif

#endif