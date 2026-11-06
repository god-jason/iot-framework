#ifndef IOT_DRV_TM1637_H
#define IOT_DRV_TM1637_H

// TM1637 2 线（CLK/DIO）7 段数码管驱动，纯 GPIO 位操作。
// 段码映射由调用方提供（seg 为段码数组，n 为位数，最多 6 位）。
#include "bus/iot_bus_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_tm1637_init(const iot_gpio_bus_t *gpio, uint8_t clk, uint8_t dio);
int iot_tm1637_display(const iot_gpio_bus_t *gpio, uint8_t clk, uint8_t dio,
                       const uint8_t *seg, uint8_t n);

#ifdef __cplusplus
}
#endif

#endif