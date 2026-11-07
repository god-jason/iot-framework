#ifndef TM1638_H
#define TM1638_H

// TM1638 3 线（STB/CLK/DIO）LED 数码管/按键驱动，纯 GPIO 位操作（简化版）。
#include "bus/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int tm1638_init(const gpio_bus_t *gpio, uint8_t stb, uint8_t clk, uint8_t dio);
int tm1638_write(const gpio_bus_t *gpio, uint8_t stb, uint8_t clk, uint8_t dio,
                     uint8_t cmd, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif