#include "hmc5883l.h"
#include "iot_logger.h"

#define HMC5883L_ADDR  0x1E
#define HMC5883L_CFGA  0x00
#define HMC5883L_CFGB  0x01
#define HMC5883L_MODE  0x02
#define HMC5883L_DATA  0x03

int hmc5883l_init(const i2c_bus_t *bus)
{
    /* 8 样本平均 / 15Hz / 正常测量；增益 ±1.3Ga；连续模式 */
    if (i2c_write_reg(bus, HMC5883L_ADDR, HMC5883L_CFGA, 0x70) != 0 ||
        i2c_write_reg(bus, HMC5883L_ADDR, HMC5883L_CFGB, 0x20) != 0 ||
        i2c_write_reg(bus, HMC5883L_ADDR, HMC5883L_MODE, 0x00) != 0) {
        LOG_E("hmc5883l", "init fail");
        return -1;
    }
    LOG_I("hmc5883l", "init ok");
    return 0;
}

int hmc5883l_read(const i2c_bus_t *bus, int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t d[6];

    if (x == NULL || y == NULL || z == NULL) {
        return -1;
    }
    if (i2c_read_regs(bus, HMC5883L_ADDR, HMC5883L_DATA, d, 6) != 0) {
        return -1;
    }
    /* 大端：X hi/lo, Z hi/lo, Y hi/lo */
    *x = (int16_t)((d[0] << 8) | d[1]);
    *z = (int16_t)((d[2] << 8) | d[3]);
    *y = (int16_t)((d[4] << 8) | d[5]);
    return 0;
}