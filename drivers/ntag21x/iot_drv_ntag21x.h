#ifndef IOT_DRV_NTAG21X_H
#define IOT_DRV_NTAG21X_H

// NTAG21x NFC 标签驱动（I2C 地址 0x55），按页读写（每页 4 字节）。
#include "bus/iot_bus_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_ntag21x_read(const iot_i2c_bus_t *bus, uint8_t page, uint8_t *data);
int iot_ntag21x_write(const iot_i2c_bus_t *bus, uint8_t page, const uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif