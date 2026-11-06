#ifndef IOT_SENSOR_BMP180_H
#define IOT_SENSOR_BMP180_H

/* BMP180 气压传感器（I2C 0x77，温压一体，校准补偿输出 ℃ 与 Pa）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：读取 0xAA 起 22 字节校准系数 */
int iot_bmp180_init(const iot_i2c_bus_t *bus);

/* 读取温度(℃)与气压(Pa)，内部读取 UT/UP 并做温度压力补偿 */
int iot_bmp180_read(const iot_i2c_bus_t *bus, float *temp, float *pressure);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_BMP180_H */