#ifndef IOT_SENSOR_VIBRATION_H
#define IOT_SENSOR_VIBRATION_H

/* 压电振动传感器：振动时输出脉冲或电平变化。 */
#include "bus/iot_bus_gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int iot_vibration_init(const iot_gpio_bus_t *bus, uint8_t pin);

/* 读取振动状态，vibrating 非空时给出是否在振动（高=1） */
int iot_vibration_read(const iot_gpio_bus_t *bus, uint8_t pin, bool *vibrating);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_VIBRATION_H */