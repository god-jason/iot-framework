#ifndef ADS1115_H
#define ADS1115_H

/* ADS1115 16 位 4 通道 Δ-Σ ADC（I2C 0x48，单次转换）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：写配置寄存器（增益等默认配置） */
int ads1115_init(const i2c_bus_t *bus);

/* 读取单端通道(0~3)的一次转换结果，输出有符号 16 位原始值 */
int ads1115_read(const i2c_bus_t *bus, uint8_t channel, int16_t *value);

#ifdef __cplusplus
}
#endif

#endif /* ADS1115_H */