#ifndef IOT_DRV_FM24CLXX_H
#define IOT_DRV_FM24CLXX_H

// FM24CLxx FRAM 铁电存储驱动（I2C 地址 0x50，FM24CL64 等）。
// FRAM 无页擦写限制，直接按字节地址读写（地址 2 字节大端）。
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_fm24clxx_write(const iot_i2c_bus_t *bus, uint16_t addr, const uint8_t *data, uint16_t len);
int iot_fm24clxx_read(const iot_i2c_bus_t *bus, uint16_t addr, uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif