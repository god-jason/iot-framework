#include "iot_sensor_am312.h"
#include "iot_logger.h"

int iot_am312_init(const iot_gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("am312", "init fail: null bus");
        return -1;
    }
    LOG_I("am312", "init ok pin %u", (unsigned int)pin);
    return 0;
}

int iot_am312_read(const iot_gpio_bus_t *bus, uint8_t pin, bool *motion)
{
    uint8_t level;

    if (bus == NULL) {
        return -1;
    }
    level = iot_gpio_get(bus, pin);
    if (motion != NULL) {
        *motion = (level != 0);
    }
    return (int)level;
}