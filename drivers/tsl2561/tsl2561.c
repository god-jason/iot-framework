#include "tsl2561.h"
#include "iot_logger.h"
#include <math.h>

#define TSL2561_ADDR 0x39

#define CMD          0x80          /* 命令位：访问寄存器时必须置位 */
#define REG_CONTROL  (CMD | 0x00)  /* 控制寄存器 */
#define REG_DATA0LOW (CMD | 0x0C)  /* CH0 低字节，连续读 0x0C..0x0F 自动递增 */
#define POWER_ON     0x03          /* 上电 */

int tsl2561_init(const i2c_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    if (i2c_write_reg(bus, TSL2561_ADDR, REG_CONTROL, POWER_ON) != 0) {
        LOG_E("tsl2561", "init fail");
        return -1;
    }
    LOG_I("tsl2561", "init ok");
    return 0;
}

int tsl2561_read(const i2c_bus_t *bus, float *lux)
{
    uint8_t d[4];
    uint16_t ch0, ch1;
    float f0, f1, ratio;

    if (bus == NULL || lux == NULL) {
        return -1;
    }
    if (i2c_read_regs(bus, TSL2561_ADDR, REG_DATA0LOW, d, 4) != 0) {
        return -1;
    }
    /* 小端：低字节在前 */
    ch0 = (uint16_t)(d[0] | (d[1] << 8));
    ch1 = (uint16_t)(d[2] | (d[3] << 8));

    if (ch0 == 0) {
        *lux = 0.0f;
        return 0;
    }
    f0 = (float)ch0;
    f1 = (float)ch1;
    ratio = f1 / f0;

    /* T 封装近似公式（饱和判断从简） */
    if (ratio <= 0.5f) {
        *lux = 0.0304f * f0 - 0.062f * f0 * powf(ratio, 1.4f);
    } else if (ratio <= 0.61f) {
        *lux = 0.0224f * f0 - 0.031f * f1;
    } else if (ratio <= 0.80f) {
        *lux = 0.0128f * f0 - 0.0153f * f1;
    } else if (ratio <= 1.30f) {
        *lux = 0.00146f * f0 - 0.00112f * f1;
    } else {
        *lux = 0.0f;
    }
    if (*lux < 0.0f) {
        *lux = 0.0f;
    }
    return 0;
}