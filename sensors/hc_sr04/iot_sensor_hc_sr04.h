#ifndef IOT_SENSOR_HC_SR04_H
#define IOT_SENSOR_HC_SR04_H

/* HC-SR04 超声波测距模块（TRIG 触发 + ECHO 回响）。 */
#include <stdint.h>
#include "bus/iot_bus_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_ultrasonic_init(iot_gpio_bus_t *bus, uint8_t trig, uint8_t echo);
int iot_ultrasonic_measure(iot_gpio_bus_t *bus, uint8_t trig, uint8_t echo, float *distance);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_HC_SR04_H */