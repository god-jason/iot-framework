#ifndef IOT_DRV_L3GD20H_H
#define IOT_DRV_L3GD20H_H

/* L3GD20H 三轴陀螺仪 I2C 传感器（默认 I2C 0x6B）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 WHO_AM_I、使能三轴 */
int iot_l3gd20h_init(const iot_i2c_bus_t *bus);

/* 读取三轴角速度原始值（int16） */
int iot_l3gd20h_read(const iot_i2c_bus_t *bus, int16_t gyro[3]);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_L3GD20H_H */