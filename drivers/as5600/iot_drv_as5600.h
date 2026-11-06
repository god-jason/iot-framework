#ifndef IOT_DRV_AS5600_H
#define IOT_DRV_AS5600_H

/* AS5600 磁编码器角度传感器 I2C 传感器（默认 I2C 0x36）。 */
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：I2C 通信自检 */
int iot_as5600_init(const iot_i2c_bus_t *bus);

/* 读取角度原始值（12 bit） */
int iot_as5600_read_angle(const iot_i2c_bus_t *bus, uint16_t *angle);

#ifdef __cplusplus
}
#endif

#endif /* IOT_DRV_AS5600_H */