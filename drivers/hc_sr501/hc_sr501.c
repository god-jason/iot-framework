#include "hc_sr501.h"
#include "iot_logger.h"

int pir_init(const gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("pir", "init fail: null bus");
        return -1;
    }
    LOG_I("pir", "init ok pin %u", (unsigned int)pin);
    return 0;
}

int pir_read(const gpio_bus_t *bus, uint8_t pin, bool *motion)
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