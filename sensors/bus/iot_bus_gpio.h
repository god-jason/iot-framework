#ifndef IOT_BUS_GPIO_H
#define IOT_BUS_GPIO_H

/*
 * 通用 GPIO 位操作总线：用于 HX711、TM1637、ST7920 等需要软件时序（bit-bang）的外设。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*set_level)(void *ctx, uint8_t pin, uint8_t level);
    uint8_t (*get_level)(void *ctx, uint8_t pin);
    void (*udelay)(void *ctx, uint32_t us);
    void *ctx;
} iot_gpio_bus_t;

void iot_gpio_set(const iot_gpio_bus_t *bus, uint8_t pin, uint8_t level);
uint8_t iot_gpio_get(const iot_gpio_bus_t *bus, uint8_t pin);
void iot_gpio_udelay(const iot_gpio_bus_t *bus, uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BUS_GPIO_H */