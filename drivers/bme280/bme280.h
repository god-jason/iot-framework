#ifndef BME280_H
#define BME280_H

/* BME280 温湿气压传感器（I2C 0x76/0x77，chip id 0xD0=0x60）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 chip id，配置温压测量与湿度测量控制 */
int bme280_init(const i2c_bus_t *bus);

/* 读取温度(℃)、气压(Pa)与湿度(%RH)并做补偿 */
int bme280_read(const i2c_bus_t *bus, float *temp, float *pressure, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* BME280_H */