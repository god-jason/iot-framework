#include "bmp388.h"
#include "iot_logger.h"

#define BMP388_ADDR    0x76
#define BMP388_CHIP_ID 0x50

#define REG_CHIP_ID  0x00
#define REG_PWR_CTRL 0x1B
#define REG_OSR      0x1C
/* 数据寄存器 0x04..0x09：压力 3 字节 + 温度 3 字节（小端） */
#define REG_DATA     0x04

int bmp388_init(const i2c_bus_t *bus)
{
    uint8_t chip = 0;

    if (bus == NULL) {
        return -1;
    }
    if (i2c_read_reg(bus, BMP388_ADDR, REG_CHIP_ID, &chip) != 0 ||
        chip != BMP388_CHIP_ID) {
        LOG_E("bmp388", "chip id fail");
        return -1;
    }
    /* normal 模式：PWR_CTRL 0x33（使能温度与压力） */
    if (i2c_write_reg(bus, BMP388_ADDR, REG_PWR_CTRL, 0x33) != 0) {
        LOG_E("bmp388", "pwr ctrl fail");
        return -1;
    }
    /* 过采样 OSR 0x03 */
    if (i2c_write_reg(bus, BMP388_ADDR, REG_OSR, 0x03) != 0) {
        LOG_E("bmp388", "osr fail");
        return -1;
    }
    LOG_I("bmp388", "init ok");
    return 0;
}

int bmp388_read(const i2c_bus_t *bus, float *temp, float *pressure)
{
    uint8_t d[6];
    uint32_t raw_p, raw_t;

    if (bus == NULL || temp == NULL || pressure == NULL) {
        return -1;
    }
    if (i2c_read_regs(bus, BMP388_ADDR, REG_DATA, d, 6) != 0) {
        return -1;
    }
    /* 压力 24bit：d[0] 最低字节 .. d[2] 最高字节（小端） */
    raw_p = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16);
    /* 温度 24bit：d[3] 最低字节 .. d[5] 最高字节（小端） */
    raw_t = (uint32_t)d[3] | ((uint32_t)d[4] << 8) | ((uint32_t)d[5] << 16);

    /* 简化：直接返回原始 24bit 值；完整补偿需读 0x31-0x54 系数后换算，
     * 此处留待外部处理。 */
    *pressure = (float)raw_p;
    *temp = (float)raw_t;
    return 0;
}