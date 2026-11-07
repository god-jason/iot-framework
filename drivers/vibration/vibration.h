#ifndef VIBRATION_H
#define VIBRATION_H

/* 压电振动传感器：振动时输出脉冲或电平变化。 */
#include "bus/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int vibration_init(const gpio_bus_t *bus, uint8_t pin);

/* 读取振动状态，vibrating 非空时给出是否在振动（高=1） */
int vibration_read(const gpio_bus_t *bus, uint8_t pin, bool *vibrating);

#ifdef __cplusplus
}
#endif

#endif /* VIBRATION_H */