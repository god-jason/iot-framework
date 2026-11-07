#ifndef FM24CLXX_H
#define FM24CLXX_H

// FM24CLxx FRAM 铁电存储驱动（I2C 地址 0x50，FM24CL64 等）。
// FRAM 无页擦写限制，直接按字节地址读写（地址 2 字节大端）。
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int fm24clxx_write(const i2c_bus_t *bus, uint16_t addr, const uint8_t *data, uint16_t len);
int fm24clxx_read(const i2c_bus_t *bus, uint16_t addr, uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif