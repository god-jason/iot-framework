#include "iot_bus_gpio.h"

void iot_gpio_set(const iot_gpio_bus_t *bus, uint8_t pin, uint8_t level)
{
    if (bus != NULL && bus->set_level != NULL) {
        bus->set_level(bus->ctx, pin, level);
    }
}

uint8_t iot_gpio_get(const iot_gpio_bus_t *bus, uint8_t pin)
{
    if (bus != NULL && bus->get_level != NULL) {
        return bus->get_level(bus->ctx, pin);
    }
    return 0;
}

void iot_gpio_udelay(const iot_gpio_bus_t *bus, uint32_t us)
{
    if (bus != NULL && bus->udelay != NULL) {
        bus->udelay(bus->ctx, us);
    }
}