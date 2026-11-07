#include "apds9960.h"
#include "iot_logger.h"

#define APDS9960_ADDR       0x39
#define APDS9960_REG_ENABLE 0x80
#define APDS9960_REG_CDATAL 0x94

int apds9960_init(const i2c_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    // ENABLE(0x80)=0x03：PON（上电）+ AEN（接近/颜色使能）
    if (i2c_write_reg(bus, APDS9960_ADDR, APDS9960_REG_ENABLE, 0x03) != 0) {
        LOG_E("apds9960", "init fail");
        return -1;
    }
    LOG_I("apds9960", "init ok");
    return 0;
}

int apds9960_read(const i2c_bus_t *bus,
                      uint16_t *c, uint16_t *r, uint16_t *g, uint16_t *b)
{
    uint8_t d[8];

    if (bus == NULL || c == NULL || r == NULL || g == NULL || b == NULL) {
        return -1;
    }
    // 颜色数据从 0x94 读 8 字节小端：C/R/G/B 各 2 字节
    if (i2c_read_regs(bus, APDS9960_ADDR, APDS9960_REG_CDATAL, d, 8) != 0) {
        return -1;
    }
    *c = (uint16_t)(d[0] | (d[1] << 8));
    *r = (uint16_t)(d[2] | (d[3] << 8));
    *g = (uint16_t)(d[4] | (d[5] << 8));
    *b = (uint16_t)(d[6] | (d[7] << 8));
    return 0;
}