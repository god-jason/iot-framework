#include "ads1115.h"
#include "iot_logger.h"

#define ADS1115_ADDR        0x48
#define ADS1115_REG_CONV    0x00    /* 转换结果寄存器 */
#define ADS1115_REG_CONFIG  0x01    /* 配置寄存器 */

int ads1115_init(const i2c_bus_t *bus)
{
    /* 高字节：PGA=±4.096V(0b001 置于 bit9)，低字节：128 SPS */
    uint8_t conf[2] = { 0x02, 0x83 };

    if (bus == NULL) {
        return -1;
    }
    if (i2c_write_regs(bus, ADS1115_ADDR, ADS1115_REG_CONFIG, conf, 2) != 0) {
        LOG_E("ads1115", "init fail");
        return -1;
    }
    LOG_I("ads1115", "init ok");
    return 0;
}

int ads1115_read(const i2c_bus_t *bus, uint8_t channel, int16_t *value)
{
    uint8_t conf[2];
    uint8_t d[2];
    int i;

    if (bus == NULL || value == NULL || channel > 3) {
        return -1;
    }
    /*
     * 配置：OS=1 启动单次转换，MUX=100+ch 选单端通道，
     * PGA=±4.096V，MODE=1 单次模式，128 SPS。
     */
    conf[0] = (uint8_t)(0xC3 | (channel << 4));
    conf[1] = 0x83;
    if (i2c_write_regs(bus, ADS1115_ADDR, ADS1115_REG_CONFIG, conf, 2) != 0) {
        return -1;
    }

    /* 轮询配置寄存器 OS 标志，等待转换完成 */
    for (i = 0; i < 1000; i++) {
        if (i2c_read_regs(bus, ADS1115_ADDR, ADS1115_REG_CONFIG, conf, 2) != 0) {
            return -1;
        }
        if (conf[0] & 0x80) {
            break;
        }
    }

    /* 读 16 位转换结果（大端） */
    if (i2c_read_regs(bus, ADS1115_ADDR, ADS1115_REG_CONV, d, 2) != 0) {
        return -1;
    }
    *value = (int16_t)(((uint16_t)d[0] << 8) | d[1]);
    return 0;
}