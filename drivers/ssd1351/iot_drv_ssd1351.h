#ifndef IOT_DRV_SSD1351_H
#define IOT_DRV_SSD1351_H

// SSD1351 128x128 RGB OLED 驱动（SPI）。
// 无 DC 线场景下用“先发命令字头”方式区分命令与数据；仅填充整屏单一颜色。
#include "bus/iot_bus_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_ssd1351_init(const iot_spi_bus_t *spi);
int iot_ssd1351_fill(const iot_spi_bus_t *spi, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif