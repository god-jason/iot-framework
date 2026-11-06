#include "iot_sensor_da267.h"
#include "iot_logger.h"

/*
 * DA267 为通用 I2C 加速度计，寄存器布局近似 LIS2DH：
 *   WHO_AM_I 0x0F、CTRL_REG1 0x20、数据 0x28（小端）。
 * 未做严格 WHO_AM_I 校验，仅按通用兼容行为初始化。
 */
#define DA267_ADDR    0x18
#define DA267_CTRL1   0x20
#define DA267_OUT_X_L 0x28

/* 100Hz 输出、使能 X/Y/Z 三轴 */
#define DA267_CTRL1_VAL 0x57

int iot_da267_init(const iot_i2c_bus_t *bus)
{
    if (bus == NULL) {
        LOG_E("da267", "bus null");
        return -1;
    }
    if (iot_i2c_write_reg(bus, DA267_ADDR, DA267_CTRL1, DA267_CTRL1_VAL) != 0) {
        LOG_E("da267", "write CTRL1 fail");
        return -1;
    }
    LOG_I("da267", "init ok");
    return 0;
}

int iot_da267_read(const iot_i2c_bus_t *bus, int16_t acc[3])
{
    uint8_t d[6];

    if (bus == NULL || acc == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, DA267_ADDR, DA267_OUT_X_L, d, 6) != 0) {
        LOG_E("da267", "read fail");
        return -1;
    }
    /* 小端解析：低字节在前 */
    acc[0] = (int16_t)(d[0] | (d[1] << 8));
    acc[1] = (int16_t)(d[2] | (d[3] << 8));
    acc[2] = (int16_t)(d[4] | (d[5] << 8));
    return 0;
}