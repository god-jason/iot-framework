#ifndef IOT_DRV_ST7920_H
#define IOT_DRV_ST7920_H

// ST7920 12864 液晶驱动（4 位并行接口 RS/RW/EN/D4-D7，GPIO 位操作）。
// 仅做基础初始化与字符串输出（使用芯片内置 ASCII 字库），不含汉字字库。
#include "bus/iot_bus_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_st7920_init(const iot_gpio_bus_t *gpio, uint8_t rs, uint8_t rw, uint8_t en,
                    uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7);
int iot_st7920_write(const iot_gpio_bus_t *gpio, const char *str);

#ifdef __cplusplus
}
#endif

#endif