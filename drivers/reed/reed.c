#include "reed.h"
#include "iot_logger.h"

int reed_init(const gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("reed", "init fail: null bus");
        return -1;
    }
    LOG_I("reed", "init ok pin %u", (unsigned int)pin);
    return 0;
}

int reed_read(const gpio_bus_t *bus, uint8_t pin, bool *closed)
{
    uint8_t level;

    if (bus == NULL || closed == NULL) {
        return -1;
    }
    level = gpio_get(bus, pin);
    *closed = (level != 0);
    return 0;
}