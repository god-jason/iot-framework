#include "iot_drv_as5600.h"
#include "iot_logger.h"

#define AS5600_ADDR       0x36
#define AS5600_REG_STATUS 0x0B
#define AS5600_REG_ANGLE  0x0E

int iot_as5600_init(const iot_i2c_bus_t *bus)
{
    uint8_t st = 0;

    if (bus == NULL) {
        LOG_E("as5600", "bus null");
        return -1;
    }
    /* 读 STATUS 做通信自检 */
    if (iot_i2c_read_reg(bus, AS5600_ADDR, AS5600_REG_STATUS, &st) != 0) {
        LOG_E("as5600", "read STATUS fail");
        return -1;
    }

    LOG_I("as5600", "init ok");
    return 0;
}

int iot_as5600_read_angle(const iot_i2c_bus_t *bus, uint16_t *angle)
{
    uint8_t d[2];
    uint16_t raw;

    if (bus == NULL || angle == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, AS5600_ADDR, AS5600_REG_ANGLE, d, 2) != 0) {
        LOG_E("as5600", "read fail");
        return -1;
    }
    /* 大端解析：高字节在前，取低 12 位 */
    raw = (uint16_t)((d[0] << 8) | d[1]);
    *angle = raw & 0x0FFFu;
    return 0;
}