#ifndef BMI270_H
#define BMI270_H

/* BMI270 六轴 IMU（I2C 0x68/0x69，16 位小端）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 简化初始化：读取芯片 ID（完整配置需上电后加载配置固件） */
int bmi270_init(const i2c_bus_t *bus);

/* 读取加速度与陀螺仪原始值（int16） */
int bmi270_read(const i2c_bus_t *bus, int16_t acc[3], int16_t gyro[3]);

#ifdef __cplusplus
}
#endif

#endif