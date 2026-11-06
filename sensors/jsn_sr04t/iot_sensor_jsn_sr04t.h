#ifndef IOT_SENSOR_JSN_SR04T_H
#define IOT_SENSOR_JSN_SR04T_H

/* JSN-SR04T 防水超声波测距模块（接口同 HC-SR04：TRIG 触发 + ECHO 回响）。 */
#include <stdint.h>
#include "bus/iot_bus_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_jsn_sr04t_init(iot_gpio_bus_t *bus, uint8_t trig, uint8_t echo);
int iot_jsn_sr04t_measure(iot_gpio_bus_t *bus, uint8_t trig, uint8_t echo, float *distance);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_JSN_SR04T_H */