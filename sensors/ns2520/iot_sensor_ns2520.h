#ifndef IOT_SENSOR_NS2520_H
#define IOT_SENSOR_NS2520_H

/* NS2520 惯性传感器（通用 I2C 布局，16 位小端）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：按通用布局配置使能三轴 */
int iot_ns2520_init(const iot_i2c_bus_t *bus);

/* 读取三轴加速度原始值（int16） */
int iot_ns2520_read(const iot_i2c_bus_t *bus, int16_t acc[3]);

#ifdef __cplusplus
}
#endif

#endif