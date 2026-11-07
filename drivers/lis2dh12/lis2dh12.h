#ifndef LIS2DH12_H
#define LIS2DH12_H

/* LIS2DH12 三轴加速度计（I2C 0x18/0x19，16 位小端）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 WHO_AM_I，配置 100Hz、使能三轴 */
int lis2dh12_init(const i2c_bus_t *bus);

/* 读取三轴加速度原始值（int16） */
int lis2dh12_read(const i2c_bus_t *bus, int16_t acc[3]);

#ifdef __cplusplus
}
#endif

#endif