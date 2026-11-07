#ifndef SSD1351_H
#define SSD1351_H

// SSD1351 128x128 RGB OLED 驱动（SPI）。
// 无 DC 线场景下用“先发命令字头”方式区分命令与数据；仅填充整屏单一颜色。
#include "bus/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

int ssd1351_init(const spi_bus_t *spi);
int ssd1351_fill(const spi_bus_t *spi, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif