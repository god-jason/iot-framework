#ifndef IOT_SENSOR_YF_S201_H
#define IOT_SENSOR_YF_S201_H

/* YF-S201 霍尔水流量传感器（方波脉冲输出）。 */
#include <stdint.h>
#include "bus/iot_bus_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_flow_init(iot_gpio_bus_t *bus, uint8_t pin);
int iot_flow_read(iot_gpio_bus_t *bus, uint8_t pin, float *liters_per_min);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_YF_S201_H */