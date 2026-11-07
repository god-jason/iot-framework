#ifndef NS2520_H
#define NS2520_H

/* NS2520 惯性传感器（通用 I2C 布局，16 位小端）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：按通用布局配置使能三轴 */
int ns2520_init(const i2c_bus_t *bus);

/* 读取三轴加速度原始值（int16） */
int ns2520_read(const i2c_bus_t *bus, int16_t acc[3]);

#ifdef __cplusplus
}
#endif

#endif