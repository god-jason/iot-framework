#include "ina226.h"
#include "iot_logger.h"

#define INA226_ADDR        0x40
#define INA226_REG_CONFIG  0x00   /* 配置寄存器 */
#define INA226_REG_SHUNT   0x01   /* 分流电压寄存器 */
#define INA226_REG_BUS     0x02   /* 总线电压寄存器 */

/* 分流电阻阻值(Ω)，可按实际硬件调整。 */
#define INA226_SHUNT_OHM    0.1f
/* 分流电压寄存器 1 LSB = 2.5 uV。 */
#define INA226_SHUNT_LSB_V  0.0000025f
/* 总线电压寄存器 1 LSB = 1.25 mV。 */
#define INA226_BUS_LSB_V    0.00125f

int ina226_init(const i2c_bus_t *bus)
{
    /* config = 0x4127：开启连续测量（分流 + 总线）。
       字段含转换时间与平均值(AVG)设置；电流校准寄存器 CAL(0x05) 默认
       0x0200 未重写，电流直接由分流电压和分流电阻换算得到。 */
    uint8_t conf[2] = { 0x41, 0x27 };

    if (bus == NULL) {
        return -1;
    }
    if (i2c_write_regs(bus, INA226_ADDR, INA226_REG_CONFIG, conf, 2) != 0) {
        LOG_E("ina226", "init fail");
        return -1;
    }
    LOG_I("ina226", "init ok");
    return 0;
}

int ina226_read(const i2c_bus_t *bus,
                    float *bus_voltage, float *current, float *power)
{
    uint8_t d[4];
    uint16_t bus_raw;
    int16_t shunt_raw;

    if (bus == NULL || bus_voltage == NULL || current == NULL || power == NULL) {
        return -1;
    }
    /* 连续读分流(0x01)与总线(0x02)两个寄存器。 */
    if (i2c_read_regs(bus, INA226_ADDR, INA226_REG_SHUNT, d, 4) != 0) {
        return -1;
    }
    /* 分流电压：有符号 16 位，1 LSB = 2.5 uV；电流 = 分流电压 / 分流电阻。 */
    shunt_raw = (int16_t)(((uint16_t)d[0] << 8) | d[1]);
    *current = (float)shunt_raw * INA226_SHUNT_LSB_V / INA226_SHUNT_OHM;

    /* 总线电压：bit15~3 有效，1 LSB = 1.25 mV。 */
    bus_raw = (uint16_t)((d[2] << 8) | d[3]);
    *bus_voltage = (float)(bus_raw >> 3) * INA226_BUS_LSB_V;

    /* 功率由电压乘电流得到；功率寄存器(0x03)的 LSB 依赖电流校准值，故不取用。 */
    *power = *bus_voltage * *current;
    return 0;
}