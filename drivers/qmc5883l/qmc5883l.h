#ifndef QMC5883L_H
#define QMC5883L_H

/* QMC5883L 三轴磁力计（I2C 0x0D，16 位小端）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int qmc5883l_init(const i2c_bus_t *bus);
int qmc5883l_read(const i2c_bus_t *bus, int16_t *x, int16_t *y, int16_t *z);

#ifdef __cplusplus
}
#endif

#endif