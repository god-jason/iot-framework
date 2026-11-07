#ifndef NTAG21X_H
#define NTAG21X_H

// NTAG21x NFC 标签驱动（I2C 地址 0x55），按页读写（每页 4 字节）。
#include "bus/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int ntag21x_read(const i2c_bus_t *bus, uint8_t page, uint8_t *data);
int ntag21x_write(const i2c_bus_t *bus, uint8_t page, const uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif