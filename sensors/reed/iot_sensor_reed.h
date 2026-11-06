#ifndef IOT_SENSOR_REED_H
#define IOT_SENSOR_REED_H

/* 磁簧开关：磁铁靠近时触点闭合，输出电平变化。 */
#include "bus/iot_bus_gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int iot_reed_init(const iot_gpio_bus_t *bus, uint8_t pin);

/* 读取磁簧状态，closed 非空时给出是否闭合（高=1，具体极性视电路而定） */
int iot_reed_read(const iot_gpio_bus_t *bus, uint8_t pin, bool *closed);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_REED_H */