#ifndef IOT_DRV_INA219_H
#define IOT_DRV_INA219_H

/* INA219 电流 / 功率监测器（I2C 0x40，测总线电压与分流电压）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：写配置寄存器（16V 总线量程等） */
int iot_ina219_init(const iot_i2c_bus_t *bus);

/* 读取总线电压(V)与负载电流(A) */
int iot_ina219_read(const iot_i2c_bus_t *bus, float *bus_voltage, float *current);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_INA219_H */