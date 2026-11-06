#ifndef IOT_SENSOR_SPL06_001_H
#define IOT_SENSOR_SPL06_001_H

/* SPL06-001 气压传感器（I2C 0x76/0x77，24bit 温压，暂近似 BMP 类返回原始值）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 id，配置连续温压测量 */
int iot_spl06_init(const iot_i2c_bus_t *bus);

/* 读取温度与气压；简化返回 24bit 原始值（补偿系数未读取） */
int iot_spl06_read(const iot_i2c_bus_t *bus, float *temp, float *pressure);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_SPL06_001_H */