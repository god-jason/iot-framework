#ifndef IOT_SENSOR_SRF05_H
#define IOT_SENSOR_SRF05_H

/* SRF05 超声波测距模块（MODE 模式引脚 + TRIG 触发 + ECHO 回响）。 */
#include <stdint.h>
#include "bus/iot_bus_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_srf05_init(iot_gpio_bus_t *bus, uint8_t trig, uint8_t echo);
int iot_srf05_measure(iot_gpio_bus_t *bus, uint8_t trig, uint8_t echo, float *distance);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_SRF05_H */