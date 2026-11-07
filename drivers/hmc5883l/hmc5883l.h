#ifndef HMC5883L_H
#define HMC5883L_H

/* HMC5883L 三轴磁力计（I2C 0x1E，16 位大端）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int hmc5883l_init(const i2c_bus_t *bus);
int hmc5883l_read(const i2c_bus_t *bus, int16_t *x, int16_t *y, int16_t *z);

#ifdef __cplusplus
}
#endif

#endif