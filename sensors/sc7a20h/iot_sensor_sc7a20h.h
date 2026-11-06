#ifndef IOT_SENSOR_SC7A20H_H
#define IOT_SENSOR_SC7A20H_H

/* SC7A20H 三轴加速度计（I2C 0x18，LIS2DH 兼容，16 位小端）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 WHO_AM_I，配置 100Hz、使能三轴 */
int iot_sc7a20h_init(const iot_i2c_bus_t *bus);

/* 读取三轴加速度原始值（int16） */
int iot_sc7a20h_read(const iot_i2c_bus_t *bus, int16_t acc[3]);

#ifdef __cplusplus
}
#endif

#endif