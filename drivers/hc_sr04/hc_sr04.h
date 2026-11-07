#ifndef HC_SR04_H
#define HC_SR04_H

/* HC-SR04 超声波测距模块（TRIG 触发 + ECHO 回响）。 */
#include <stdint.h>
#include "bus/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int ultrasonic_init(gpio_bus_t *bus, uint8_t trig, uint8_t echo);
int ultrasonic_measure(gpio_bus_t *bus, uint8_t trig, uint8_t echo, float *distance);

#ifdef __cplusplus
}
#endif

#endif /* HC_SR04_H */