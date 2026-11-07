#include "qmc5883l.h"
#include "iot_logger.h"

#define QMC5883L_ADDR  0x0D
#define QMC5883L_DATA  0x00
#define QMC5883L_CFG   0x09

int qmc5883l_init(const i2c_bus_t *bus)
{
    /* 0x09 = 连续模式, 100Hz, ±2G, OSR512 */
    int r = i2c_write_reg(bus, QMC5883L_ADDR, QMC5883L_CFG, 0x09);
    LOG_I("qmc5883l", "init %s", r == 0 ? "ok" : "fail");
    return r;
}

int qmc5883l_read(const i2c_bus_t *bus, int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t d[6];

    if (x == NULL || y == NULL || z == NULL) {
        return -1;
    }
    if (i2c_read_regs(bus, QMC5883L_ADDR, QMC5883L_DATA, d, 6) != 0) {
        return -1;
    }
    *x = (int16_t)(d[0] | (d[1] << 8));
    *y = (int16_t)(d[2] | (d[3] << 8));
    *z = (int16_t)(d[4] | (d[5] << 8));
    return 0;
}