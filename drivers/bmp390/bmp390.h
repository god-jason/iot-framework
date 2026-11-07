#ifndef BMP390_H
#define BMP390_H

// BMP390 气压传感器（I2C 0x76，chip id 0x60，寄存器式）。
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化（校验 chip id，配置 normal 模式） */
int bmp390_init(const i2c_bus_t *bus);

/* 读取温度(℃)与气压(Pa)；简化实现直接返回原始 24bit 值 */
int bmp390_read(const i2c_bus_t *bus, float *temp, float *pressure);

#ifdef __cplusplus
}
#endif

#endif /* BMP390_H */