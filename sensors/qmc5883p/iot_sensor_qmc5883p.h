#ifndef IOT_SENSOR_QMC5883P_H
#define IOT_SENSOR_QMC5883P_H

/* QMC5883P 三轴磁力计（I2C 0x2C，16 位小端，宽量程）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_qmc5883p_init(const iot_i2c_bus_t *bus);
int iot_qmc5883p_read(const iot_i2c_bus_t *bus, int16_t *x, int16_t *y, int16_t *z);

#ifdef __cplusplus
}
#endif

#endif