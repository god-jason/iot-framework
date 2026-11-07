#ifndef GPIO_H
#define GPIO_H

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
} gpio_bus_t;

void gpio_set(const gpio_bus_t *bus, uint8_t pin, uint8_t level);
uint8_t gpio_get(const gpio_bus_t *bus, uint8_t pin);
void gpio_udelay(const gpio_bus_t *bus, uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_H */