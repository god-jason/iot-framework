#ifndef IOT_DRV_MAX7219_H
#define IOT_DRV_MAX7219_H

// MAX7219 LED 点阵/数码管驱动（SPI），16 位为一帧：高 8 位地址、低 8 位数据。
#include "bus/iot_bus_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_max7219_init(const iot_spi_bus_t *spi);
int iot_max7219_write(const iot_spi_bus_t *spi, uint8_t addr, uint8_t data);
int iot_max7219_clear(const iot_spi_bus_t *spi);

#ifdef __cplusplus
}
#endif

#endif