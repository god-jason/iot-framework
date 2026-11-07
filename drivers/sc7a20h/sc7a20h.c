#include "sc7a20h.h"
#include "iot_logger.h"

#define SC7A20H_ADDR         0x18
#define SC7A20H_REG_WHO_AM_I 0x0F
#define SC7A20H_REG_CTRL1    0x20
#define SC7A20H_REG_OUT_X_L  0x28

#define SC7A20H_WHO_AM_I_VAL 0x11
/* 100Hz 输出、使能 X/Y/Z 三轴 */
#define SC7A20H_CTRL1_VAL    0x57

int sc7a20h_init(const i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        LOG_E("sc7a20h", "bus null");
        return -1;
    }
    /* WHO_AM_I 应读回 0x11 */
    if (i2c_read_reg(bus, SC7A20H_ADDR, SC7A20H_REG_WHO_AM_I, &id) != 0) {
        LOG_E("sc7a20h", "read WHO_AM_I fail");
        return -1;
    }
    if (id != SC7A20H_WHO_AM_I_VAL) {
        LOG_E("sc7a20h", "bad id 0x%02X", id);
        return -1;
    }
    /* CTRL_REG1 = 100Hz、三轴使能、普通模式 */
    if (i2c_write_reg(bus, SC7A20H_ADDR, SC7A20H_REG_CTRL1, SC7A20H_CTRL1_VAL) != 0) {
        LOG_E("sc7a20h", "write CTRL1 fail");
        return -1;
    }
    LOG_I("sc7a20h", "init ok");
    return 0;
}

int sc7a20h_read(const i2c_bus_t *bus, int16_t acc[3])
{
    uint8_t d[6];

    if (bus == NULL || acc == NULL) {
        return -1;
    }
    if (i2c_read_regs(bus, SC7A20H_ADDR, SC7A20H_REG_OUT_X_L, d, 6) != 0) {
        LOG_E("sc7a20h", "read fail");
        return -1;
    }
    /* 小端解析：低字节在前 */
    acc[0] = (int16_t)(d[0] | (d[1] << 8));
    acc[1] = (int16_t)(d[2] | (d[3] << 8));
    acc[2] = (int16_t)(d[4] | (d[5] << 8));
    return 0;
}