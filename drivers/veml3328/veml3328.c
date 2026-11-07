#include "veml3328.h"
#include "iot_logger.h"

#define VEML3328_ADDR       0x10
#define VEML3328_REG_CONFIG 0x00
#define VEML3328_REG_DATA   0x05

int veml3328_init(const i2c_bus_t *bus)
{
    if (bus == NULL) {
        return -1;
    }
    // 使能传感器（SD=0），增益 1x，关闭自动补偿
    if (i2c_write_reg(bus, VEML3328_ADDR, VEML3328_REG_CONFIG, 0x00) != 0) {
        LOG_E("veml3328", "init fail");
        return -1;
    }
    LOG_I("veml3328", "init ok");
    return 0;
}

int veml3328_read(const i2c_bus_t *bus,
                      uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *ir)
{
    uint8_t d[8];

    if (bus == NULL || r == NULL || g == NULL || b == NULL || ir == NULL) {
        return -1;
    }
    // 数据寄存器 0x05-0x0C：R/G/B/IR 各 2 字节小端
    if (i2c_read_regs(bus, VEML3328_ADDR, VEML3328_REG_DATA, d, 8) != 0) {
        return -1;
    }
    *r  = (uint16_t)(d[0] | (d[1] << 8));
    *g  = (uint16_t)(d[2] | (d[3] << 8));
    *b  = (uint16_t)(d[4] | (d[5] << 8));
    *ir = (uint16_t)(d[6] | (d[7] << 8));
    return 0;
}