#ifndef IOT_DRV_BH1750_H
#define IOT_DRV_BH1750_H

/* BH1750FVI 环境光传感器（I2C 0x23，命令式无寄存器）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_bh1750_init(const iot_i2c_bus_t *bus);
int iot_bh1750_read(const iot_i2c_bus_t *bus, float *lux);

#ifdef __cplusplus
}
#endif

#endif