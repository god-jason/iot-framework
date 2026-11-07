#include "gpio.h"

void gpio_set(const gpio_bus_t *bus, uint8_t pin, uint8_t level)
{
    if (bus != NULL && bus->set_level != NULL) {
        bus->set_level(bus->ctx, pin, level);
    }
}

uint8_t gpio_get(const gpio_bus_t *bus, uint8_t pin)
{
    if (bus != NULL && bus->get_level != NULL) {
        return bus->get_level(bus->ctx, pin);
    }
    return 0;
}

void gpio_udelay(const gpio_bus_t *bus, uint32_t us)
{
    if (bus != NULL && bus->udelay != NULL) {
        bus->udelay(bus->ctx, us);
    }
}