#ifndef IOT_SENSOR_MPU9250_H
#define IOT_SENSOR_MPU9250_H

/* MPU9250 六轴 + 磁力计（I2C 0x68，16 位大端）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 WHO_AM_I、唤醒设备 */
int iot_mpu9250_init(const iot_i2c_bus_t *bus);

/* 读取加速度与陀螺仪原始值（int16） */
int iot_mpu9250_read(const iot_i2c_bus_t *bus, int16_t acc[3], int16_t gyro[3]);

#ifdef __cplusplus
}
#endif

#endif