#ifndef MAX7219_H
#define MAX7219_H

// MAX7219 LED 点阵/数码管驱动（SPI），16 位为一帧：高 8 位地址、低 8 位数据。
#include "bus/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

int max7219_init(const spi_bus_t *spi);
int max7219_write(const spi_bus_t *spi, uint8_t addr, uint8_t data);
int max7219_clear(const spi_bus_t *spi);

#ifdef __cplusplus
}
#endif

#endif