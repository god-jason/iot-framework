#ifndef SHT3X_H
#define SHT3X_H

// SHT3x 温湿度传感器（I2C 0x44，命令式，单次测量 0x2C 0x06）。
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（软复位） */
int sht3x_init(const i2c_bus_t *bus);

/* 单次测量并读取温度(℃)与湿度(%RH)，忽略 CRC */
int sht3x_read(const i2c_bus_t *bus, float *temp, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* SHT3X_H */