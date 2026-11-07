#ifndef BMP280_H
#define BMP280_H

/* BMP280 气压传感器（I2C 0x76/0x77，chip id 0xD0=0x58，温压一体）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 chip id 并配置 ctrl_meas = 0x27 */
int bmp280_init(const i2c_bus_t *bus);

/* 读取温度(℃)与气压(Pa)，读 0xF7 起 6 字节并做补偿 */
int bmp280_read(const i2c_bus_t *bus, float *temp, float *pressure);

#ifdef __cplusplus
}
#endif

#endif /* BMP280_H */