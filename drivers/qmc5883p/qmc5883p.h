#ifndef QMC5883P_H
#define QMC5883P_H

/* QMC5883P 三轴磁力计（I2C 0x2C，16 位小端，宽量程）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int qmc5883p_init(const i2c_bus_t *bus);
int qmc5883p_read(const i2c_bus_t *bus, int16_t *x, int16_t *y, int16_t *z);

#ifdef __cplusplus
}
#endif

#endif