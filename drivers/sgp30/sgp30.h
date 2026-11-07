#ifndef SGP30_H
#define SGP30_H

/* SGP30 空气质量传感器（I2C 0x58，CO₂ + TVOC，命令式）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int sgp30_init(const i2c_bus_t *bus);

/* 测量 CO₂(ppm) 与 TVOC(ppb)；触发后需 ≥12ms 延时可读 */
int sgp30_measure(const i2c_bus_t *bus, uint16_t *co2, uint16_t *tvoc);

#ifdef __cplusplus
}
#endif

#endif