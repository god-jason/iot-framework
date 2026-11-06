#ifndef IOT_SENSOR_BMI270_H
#define IOT_SENSOR_BMI270_H

/* BMI270 六轴 IMU（I2C 0x68/0x69，16 位小端）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 简化初始化：读取芯片 ID（完整配置需上电后加载配置固件） */
int iot_bmi270_init(const iot_i2c_bus_t *bus);

/* 读取加速度与陀螺仪原始值（int16） */
int iot_bmi270_read(const iot_i2c_bus_t *bus, int16_t acc[3], int16_t gyro[3]);

#ifdef __cplusplus
}
#endif

#endif