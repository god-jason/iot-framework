#ifndef IOT_DRV_SCD30_H
#define IOT_DRV_SCD30_H

// SCD30 CO2/温湿度传感器（I2C 0x61，命令式）。
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（启动连续测量） */
int iot_scd30_init(const iot_i2c_bus_t *bus);

/* 读取 CO2(ppm)、温度(℃)、湿度(%RH)；未就绪返回负数 */
int iot_scd30_read(const iot_i2c_bus_t *bus, float *co2, float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_SCD30_H */