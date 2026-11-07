#ifndef CCS811_H
#define CCS811_H

/* CCS811 eCO2 / TVOC 气体传感器（I2C 0x5A，命令式）。 */
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：校验 hw_id 并启动 APP（写 0xF4 = 0x01） */
int ccs811_init(const i2c_bus_t *bus);

/* 读取 eCO2(ppm) 与 TVOC(ppb)，未就绪返回负值 */
int ccs811_read(const i2c_bus_t *bus, uint16_t *co2, uint16_t *tvoc);

#ifdef __cplusplus
}
#endif

#endif /* CCS811_H */