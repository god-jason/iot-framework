#ifndef AM312_H
#define AM312_H

/* AM312 人体红外（PIR）传感器：与 HC-SR501 同款，检测到移动时输出高电平。 */
#include "bus/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int am312_init(const gpio_bus_t *bus, uint8_t pin);

/* 读取输出电平，返回原始电平（get_level）；motion 非空时给出是否检测到移动 */
int am312_read(const gpio_bus_t *bus, uint8_t pin, bool *motion);

#ifdef __cplusplus
}
#endif

#endif /* AM312_H */