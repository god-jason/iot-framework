#ifndef HC_SR501_H
#define HC_SR501_H

/* HC-SR501 人体红外（PIR）传感器：检测到人体移动时输出高电平。 */
#include "bus/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int pir_init(const gpio_bus_t *bus, uint8_t pin);

/* 读取输出电平，返回原始电平（get_level）；motion 非空时给出是否检测到移动 */
int pir_read(const gpio_bus_t *bus, uint8_t pin, bool *motion);

#ifdef __cplusplus
}
#endif

#endif /* HC_SR501_H */