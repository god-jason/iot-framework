#include "opt3001.h"
#include "iot_logger.h"

#include <math.h>

#define OPT3001_ADDR        0x44
#define OPT3001_REG_RESULT  0x00
#define OPT3001_REG_CONFIG  0x01

int opt3001_init(const i2c_bus_t *bus)
{
    // 连续转换 + 自动量程：0xCC10（MOD=10 连续模式，FC=1 自动量程）
    uint8_t cfg[2] = { 0xCC, 0x10 };

    if (bus == NULL) {
        return -1;
    }
    if (i2c_write_regs(bus, OPT3001_ADDR, OPT3001_REG_CONFIG, cfg, 2) != 0) {
        LOG_E("opt3001", "init fail");
        return -1;
    }
    LOG_I("opt3001", "init ok");
    return 0;
}

int opt3001_read(const i2c_bus_t *bus, float *lux)
{
    uint8_t  d[2];
    uint16_t raw;
    uint8_t  exp;
    uint16_t lsb;

    if (bus == NULL || lux == NULL) {
        return -1;
    }
    if (i2c_read_regs(bus, OPT3001_ADDR, OPT3001_REG_RESULT, d, 2) != 0) {
        return -1;
    }
    // 寄存器 0x00 读 2 字节大端
    raw = (uint16_t)((d[0] << 8) | d[1]);
    // 高 12 位为结果（LSB），低 4 位为指数
    exp = (uint8_t)(raw & 0x0F);
    lsb = (uint16_t)((raw >> 4) & 0x0FFF);
    // lux = LSB * 0.01 * 2^exp
    *lux = (float)lsb * 0.01f * ldexpf(1.0f, (int)exp);
    return 0;
}