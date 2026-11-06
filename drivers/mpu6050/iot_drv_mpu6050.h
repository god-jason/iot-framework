#ifndef IOT_DRV_MPU6050_H
#define IOT_DRV_MPU6050_H

/* MPU6050 六轴（3 轴加速度 + 3 轴角速度）I2C 传感器（默认 I2C 0x68）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 WHO_AM_I、唤醒、设置样本率分频与满量程 */
int iot_mpu6050_init(const iot_i2c_bus_t *bus);

/* 读取加速度与角速度原始值（各 3 轴 int16） */
int iot_mpu6050_read(const iot_i2c_bus_t *bus, int16_t acc[3], int16_t gyro[3]);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_MPU6050_H */