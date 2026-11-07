#include "am312.h"
#include "iot_logger.h"

int am312_init(const gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("am312", "init fail: null bus");
        return -1;
    }
    LOG_I("am312", "init ok pin %u", (unsigned int)pin);
    return 0;
}

int am312_read(const gpio_bus_t *bus, uint8_t pin, bool *motion)
{
    uint8_t level;

    if (bus == NULL) {
        return -1;
    }
    level = gpio_get(bus, pin);
    if (motion != NULL) {
        *motion = (level != 0);
    }
    return (int)level;
}