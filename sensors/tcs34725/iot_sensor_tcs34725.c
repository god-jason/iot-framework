#include "iot_sensor_tcs34725.h"
#include "iot_logger.h"

#define TCS34725_ADDR       0x29
#define TCS34725_REG_ENABLE 0x00
#define TCS34725_REG_CDATAL 0x14

int iot_tcs34725_init(const iot_i2c_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    // ENABLE=0x03：AEN（RGBC 使能）+ PON（上电）
    if (iot_i2c_write_reg(bus, TCS34725_ADDR, TCS34725_REG_ENABLE, 0x03) != 0) {
        LOG_E("tcs34725", "init fail");
        return -1;
    }
    LOG_I("tcs34725", "init ok");
    return 0;
}

int iot_tcs34725_read(const iot_i2c_bus_t *bus,
                      uint16_t *c, uint16_t *r, uint16_t *g, uint16_t *b)
{
    uint8_t d[8];

    if (bus == NULL || c == NULL || r == NULL || g == NULL || b == NULL) {
        return -1;
    }
    // 从 0x14 读 8 字节小端：C/R/G/B 各 2 字节
    if (iot_i2c_read_regs(bus, TCS34725_ADDR, TCS34725_REG_CDATAL, d, 8) != 0) {
        return -1;
    }
    *c = (uint16_t)(d[0] | (d[1] << 8));
    *r = (uint16_t)(d[2] | (d[3] << 8));
    *g = (uint16_t)(d[4] | (d[5] << 8));
    *b = (uint16_t)(d[6] | (d[7] << 8));
    return 0;
}