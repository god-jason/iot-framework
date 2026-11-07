#ifndef INA219_H
#define INA219_H

/* INA219 电流 / 功率监测器（I2C 0x40，测总线电压与分流电压）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：写配置寄存器（16V 总线量程等） */
int ina219_init(const i2c_bus_t *bus);

/* 读取总线电压(V)与负载电流(A) */
int ina219_read(const i2c_bus_t *bus, float *bus_voltage, float *current);

#ifdef __cplusplus
}
#endif

#endif /* INA219_H */