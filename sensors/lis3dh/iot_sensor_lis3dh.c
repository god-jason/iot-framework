#include "iot_sensor_lis3dh.h"
#include "iot_logger.h"

#define LIS3DH_ADDR         0x18
#define LIS3DH_REG_WHO_AM_I 0x0F
#define LIS3DH_REG_CTRL1    0x20
#define LIS3DH_REG_OUT_X_L  0x28

#define LIS3DH_WHO_AM_I_VAL 0x33
/* 100Hz 输出、使能 X/Y/Z 三轴 */
#define LIS3DH_CTRL1_VAL    0x57

int iot_lis3dh_init(const iot_i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        LOG_E("lis3dh", "bus null");
        return -1;
    }
    /* WHO_AM_I 应读回 0x33 */
    if (iot_i2c_read_reg(bus, LIS3DH_ADDR, LIS3DH_REG_WHO_AM_I, &id) != 0) {
        LOG_E("lis3dh", "read WHO_AM_I fail");
        return -1;
    }
    if (id != LIS3DH_WHO_AM_I_VAL) {
        LOG_E("lis3dh", "bad id 0x%02X", id);
        return -1;
    }
    /* CTRL_REG1 = 100Hz、三轴使能、普通模式 */
    if (iot_i2c_write_reg(bus, LIS3DH_ADDR, LIS3DH_REG_CTRL1, LIS3DH_CTRL1_VAL) != 0) {
        LOG_E("lis3dh", "write CTRL1 fail");
        return -1;
    }
    LOG_I("lis3dh", "init ok");
    return 0;
}

int iot_lis3dh_read(const iot_i2c_bus_t *bus, int16_t acc[3])
{
    uint8_t d[6];

    if (bus == NULL || acc == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, LIS3DH_ADDR, LIS3DH_REG_OUT_X_L, d, 6) != 0) {
        LOG_E("lis3dh", "read fail");
        return -1;
    }
    /* 小端解析：低字节在前 */
    acc[0] = (int16_t)(d[0] | (d[1] << 8));
    acc[1] = (int16_t)(d[2] | (d[3] << 8));
    acc[2] = (int16_t)(d[4] | (d[5] << 8));
    return 0;
}