#ifndef SRF05_H
#define SRF05_H

/* SRF05 超声波测距模块（MODE 模式引脚 + TRIG 触发 + ECHO 回响）。 */
#include <stdint.h>
#include "bus/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int srf05_init(gpio_bus_t *bus, uint8_t trig, uint8_t echo);
int srf05_measure(gpio_bus_t *bus, uint8_t trig, uint8_t echo, float *distance);

#ifdef __cplusplus
}
#endif

#endif /* SRF05_H */