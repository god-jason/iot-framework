#ifndef REED_H
#define REED_H

/* 磁簧开关：磁铁靠近时触点闭合，输出电平变化。 */
#include "bus/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int reed_init(const gpio_bus_t *bus, uint8_t pin);

/* 读取磁簧状态，closed 非空时给出是否闭合（高=1，具体极性视电路而定） */
int reed_read(const gpio_bus_t *bus, uint8_t pin, bool *closed);

#ifdef __cplusplus
}
#endif

#endif /* REED_H */