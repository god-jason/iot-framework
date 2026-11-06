#include "iot_sensor_a3144.h"
#include "iot_logger.h"

int iot_hall_init(const iot_gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("hall", "init fail: null bus");
        return -1;
    }
    LOG_I("hall", "init ok pin %u", (unsigned int)pin);
    return 0;
}

int iot_hall_read(const iot_gpio_bus_t *bus, uint8_t pin, bool *detected)
{
    uint8_t level;

    if (bus == NULL || detected == NULL) {
        return -1;
    }
    level = iot_gpio_get(bus, pin);
    *detected = (level != 0);
    return 0;
}