#ifndef INA226_H
#define INA226_H

/* INA226 电流 / 电压 / 功率监测器（I2C 0x40）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int ina226_init(const i2c_bus_t *bus);

/* 读取总线电压(V)、电流(A)与功率(W)。 */
int ina226_read(const i2c_bus_t *bus,
                    float *bus_voltage, float *current, float *power);

#ifdef __cplusplus
}
#endif

#endif /* INA226_H */