#ifndef L3GD20H_H
#define L3GD20H_H

/* L3GD20H 三轴陀螺仪 I2C 传感器（默认 I2C 0x6B）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 WHO_AM_I、使能三轴 */
int l3gd20h_init(const i2c_bus_t *bus);

/* 读取三轴角速度原始值（int16） */
int l3gd20h_read(const i2c_bus_t *bus, int16_t gyro[3]);

#ifdef __cplusplus
}
#endif

#endif /* L3GD20H_H */