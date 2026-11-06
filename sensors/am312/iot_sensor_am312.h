#ifndef IOT_SENSOR_AM312_H
#define IOT_SENSOR_AM312_H

/* AM312 人体红外（PIR）传感器：与 HC-SR501 同款，检测到移动时输出高电平。 */
#include "bus/iot_bus_gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int iot_am312_init(const iot_gpio_bus_t *bus, uint8_t pin);

/* 读取输出电平，返回原始电平（get_level）；motion 非空时给出是否检测到移动 */
int iot_am312_read(const iot_gpio_bus_t *bus, uint8_t pin, bool *motion);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_AM312_H */