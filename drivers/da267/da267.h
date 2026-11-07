#ifndef DA267_H
#define DA267_H

/* DA267 三轴加速度计（通用 I2C 布局，近似 LIS2DH，16 位小端）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：按通用 LIS2DH 布局配置 100Hz、使能三轴 */
int da267_init(const i2c_bus_t *bus);

/* 读取三轴加速度原始值（int16） */
int da267_read(const i2c_bus_t *bus, int16_t acc[3]);

#ifdef __cplusplus
}
#endif

#endif