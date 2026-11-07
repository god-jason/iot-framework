#ifndef YF_S201_H
#define YF_S201_H

/* YF-S201 霍尔水流量传感器（方波脉冲输出）。 */
#include <stdint.h>
#include "bus/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int flow_init(gpio_bus_t *bus, uint8_t pin);
int flow_read(gpio_bus_t *bus, uint8_t pin, float *liters_per_min);

#ifdef __cplusplus
}
#endif

#endif /* YF_S201_H */