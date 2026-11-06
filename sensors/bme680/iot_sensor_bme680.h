#ifndef IOT_SENSOR_BME680_H
#define IOT_SENSOR_BME680_H

/* BME680 气体/温湿气压传感器（I2C 0x76/0x77，chip id 0xD0=0x60）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 chip id，配置温湿压测量（简化，气体测量未完整配置） */
int iot_bme680_init(const iot_i2c_bus_t *bus);

/* 读取温湿压原始值；气体电阻补偿复杂，暂返回 0 并注释 */
int iot_bme680_read(const iot_i2c_bus_t *bus, float *temp, float *pressure,
                    float *humidity, float *gas_resistance);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_BME680_H */