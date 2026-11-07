#ifndef ADXL345_H
#define ADXL345_H

/* ADXL345 三轴加速度计 I2C 传感器（默认 I2C 0x53）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 DEVID、进入测量模式、设置数据格式 */
int adxl345_init(const i2c_bus_t *bus);

/* 读取三轴加速度原始值（int16） */
int adxl345_read(const i2c_bus_t *bus, int16_t acc[3]);

#ifdef __cplusplus
}
#endif

#endif /* ADXL345_H */