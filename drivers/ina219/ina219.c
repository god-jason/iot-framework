#include "ina219.h"
#include "iot_logger.h"

#define INA219_ADDR         0x40
#define INA219_REG_CONFIG   0x00    /* 配置寄存器 */
#define INA219_REG_SHUNT    0x01    /* 分流电压寄存器 */
#define INA219_REG_BUS      0x02    /* 总线电压寄存器 */

/* 分流电阻阻值(Ω)，可按实际硬件调整 */
#define INA219_SHUNT_OHM    0.1f

int ina219_init(const i2c_bus_t *bus)
{
    /* 配置：BRNG=0 总线量程 16V，PG=0b11 分流量程 320mV，12 位 ADC */
    uint8_t conf[2] = { 0x39, 0x9F };

    if (bus == NULL) {
        return -1;
    }
    if (i2c_write_regs(bus, INA219_ADDR, INA219_REG_CONFIG, conf, 2) != 0) {
        LOG_E("ina219", "init fail");
        return -1;
    }
    LOG_I("ina219", "init ok");
    return 0;
}

int ina219_read(const i2c_bus_t *bus, float *bus_voltage, float *current)
{
    uint8_t d[2];
    uint16_t bus_raw;
    int16_t shunt_raw;

    if (bus == NULL || bus_voltage == NULL || current == NULL) {
        return -1;
    }

    /* 总线电压：bit15~3 有效，1 LSB = 4mV */
    if (i2c_read_regs(bus, INA219_ADDR, INA219_REG_BUS, d, 2) != 0) {
        return -1;
    }
    bus_raw = (uint16_t)((d[0] << 8) | d[1]);
    *bus_voltage = (float)(bus_raw >> 3) * 0.004f;

    /* 分流电压：有符号 16 位，1 LSB = 10µV；电流 = 分流电压 / 分流电阻 */
    if (i2c_read_regs(bus, INA219_ADDR, INA219_REG_SHUNT, d, 2) != 0) {
        return -1;
    }
    shunt_raw = (int16_t)(((uint16_t)d[0] << 8) | d[1]);
    *current = (float)shunt_raw * 1e-5f / INA219_SHUNT_OHM;

    return 0;
}