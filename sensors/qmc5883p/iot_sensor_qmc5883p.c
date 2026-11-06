#include "iot_sensor_qmc5883p.h"
#include "iot_logger.h"

#define QMC5883P_ADDR  0x2C
#define QMC5883P_DATA  0x00
#define QMC5883P_CFG   0x09

int iot_qmc5883p_init(const iot_i2c_bus_t *bus)
{
    int r = iot_i2c_write_reg(bus, QMC5883P_ADDR, QMC5883P_CFG, 0x09);
    LOG_I("qmc5883p", "init %s", r == 0 ? "ok" : "fail");
    return r;
}

int iot_qmc5883p_read(const iot_i2c_bus_t *bus, int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t d[6];

    if (x == NULL || y == NULL || z == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, QMC5883P_ADDR, QMC5883P_DATA, d, 6) != 0) {
        return -1;
    }
    *x = (int16_t)(d[0] | (d[1] << 8));
    *y = (int16_t)(d[2] | (d[3] << 8));
    *z = (int16_t)(d[4] | (d[5] << 8));
    return 0;
}