#ifndef BH1750_H
#define BH1750_H

/* BH1750FVI 环境光传感器（I2C 0x23，命令式无寄存器）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int bh1750_init(const i2c_bus_t *bus);
int bh1750_read(const i2c_bus_t *bus, float *lux);

#ifdef __cplusplus
}
#endif

#endif