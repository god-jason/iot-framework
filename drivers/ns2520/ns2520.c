#include "ns2520.h"
#include "iot_logger.h"

/*
 * NS2520 为通用惯性传感器，寄存器布局按 LIS2DH 类近似处理：
 *   WHO_AM_I 0x0F、CTRL_REG1 0x20、数据 0x28（小端）。
 * 未做严格 WHO_AM_I 校验，仅按通用兼容行为初始化。
 */
#define NS2520_ADDR    0x18
#define NS2520_CTRL1   0x20
#define NS2520_OUT_X_L 0x28

/* 100Hz 输出、使能 X/Y/Z 三轴 */
#define NS2520_CTRL1_VAL 0x57

int ns2520_init(const i2c_bus_t *bus)
{
    if (bus == NULL) {
        LOG_E("ns2520", "bus null");
        return -1;
    }
    if (i2c_write_reg(bus, NS2520_ADDR, NS2520_CTRL1, NS2520_CTRL1_VAL) != 0) {
        LOG_E("ns2520", "write CTRL1 fail");
        return -1;
    }
    LOG_I("ns2520", "init ok");
    return 0;
}

int ns2520_read(const i2c_bus_t *bus, int16_t acc[3])
{
    uint8_t d[6];

    if (bus == NULL || acc == NULL) {
        return -1;
    }
    if (i2c_read_regs(bus, NS2520_ADDR, NS2520_OUT_X_L, d, 6) != 0) {
        LOG_E("ns2520", "read fail");
        return -1;
    }
    /* 小端解析：低字节在前 */
    acc[0] = (int16_t)(d[0] | (d[1] << 8));
    acc[1] = (int16_t)(d[2] | (d[3] << 8));
    acc[2] = (int16_t)(d[4] | (d[5] << 8));
    return 0;
}