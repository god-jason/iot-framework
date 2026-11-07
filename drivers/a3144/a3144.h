#ifndef A3144_H
#define A3144_H

/* A3144 霍尔开关：磁场靠近时输出电平变化（开漏输出，常需上拉）。 */
#include "bus/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int hall_init(const gpio_bus_t *bus, uint8_t pin);

/* 读取霍尔输出，detected 非空时给出电平状态（高=1，具体极性视电路而定） */
int hall_read(const gpio_bus_t *bus, uint8_t pin, bool *detected);

#ifdef __cplusplus
}
#endif

#endif /* A3144_H */