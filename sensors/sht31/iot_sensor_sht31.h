#ifndef IOT_SENSOR_SHT31_H
#define IOT_SENSOR_SHT31_H

/* SHT31 温湿度传感器（I2C 0x44/0x45，协议同 SHT30）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（软复位） */
int iot_sht31_init(const iot_i2c_bus_t *bus);

/* 单次测量并读取温度(℃)与湿度(%RH)，忽略 CRC */
int iot_sht31_read(const iot_i2c_bus_t *bus, float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_SHT31_H */