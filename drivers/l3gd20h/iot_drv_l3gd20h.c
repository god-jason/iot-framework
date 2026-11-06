#include "iot_drv_l3gd20h.h"
#include "iot_logger.h"

#define L3GD20H_ADDR         0x6B
#define L3GD20H_REG_WHO_AM_I 0x0F
#define L3GD20H_REG_CTRL1    0x20
#define L3GD20H_REG_OUT_X_L  0x28

#define L3GD20H_WHO_AM_I_VAL 0xD7

int iot_l3gd20h_init(const iot_i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        LOG_E("l3gd20h", "bus null");
        return -1;
    }
    /* WHO_AM_I 应读回 0xD7 */
    if (iot_i2c_read_reg(bus, L3GD20H_ADDR, L3GD20H_REG_WHO_AM_I, &id) != 0) {
        LOG_E("l3gd20h", "read WHO_AM_I fail");
        return -1;
    }
    if (id != L3GD20H_WHO_AM_I_VAL) {
        LOG_E("l3gd20h", "bad id 0x%02X", id);
        return -1;
    }
    /* CTRL1 = 0x0F：使能三轴、正常模式 */
    iot_i2c_write_reg(bus, L3GD20H_ADDR, L3GD20H_REG_CTRL1, 0x0F);

    LOG_I("l3gd20h", "init ok");
    return 0;
}

int iot_l3gd20h_read(const iot_i2c_bus_t *bus, int16_t gyro[3])
{
    uint8_t d[6];

    if (bus == NULL || gyro == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, L3GD20H_ADDR, L3GD20H_REG_OUT_X_L, d, 6) != 0) {
        LOG_E("l3gd20h", "read fail");
        return -1;
    }
    /* 小端解析：低字节在前 */
    gyro[0] = (int16_t)(d[0] | (d[1] << 8));
    gyro[1] = (int16_t)(d[2] | (d[3] << 8));
    gyro[2] = (int16_t)(d[4] | (d[5] << 8));
    return 0;
}