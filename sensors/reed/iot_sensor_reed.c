#include "iot_sensor_reed.h"
#include "iot_logger.h"

int iot_reed_init(const iot_gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("reed", "init fail: null bus");
        return -1;
    }
    LOG_I("reed", "init ok pin %u", (unsigned int)pin);
    return 0;
}

int iot_reed_read(const iot_gpio_bus_t *bus, uint8_t pin, bool *closed)
{
    uint8_t level;

    if (bus == NULL || closed == NULL) {
        return -1;
    }
    level = iot_gpio_get(bus, pin);
    *closed = (level != 0);
    return 0;
}