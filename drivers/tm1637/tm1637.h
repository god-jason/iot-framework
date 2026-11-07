#ifndef TM1637_H
#define TM1637_H

// TM1637 2 线（CLK/DIO）7 段数码管驱动，纯 GPIO 位操作。
// 段码映射由调用方提供（seg 为段码数组，n 为位数，最多 6 位）。
#include "bus/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int tm1637_init(const gpio_bus_t *gpio, uint8_t clk, uint8_t dio);
int tm1637_display(const gpio_bus_t *gpio, uint8_t clk, uint8_t dio,
                       const uint8_t *seg, uint8_t n);

#ifdef __cplusplus
}
#endif

#endif