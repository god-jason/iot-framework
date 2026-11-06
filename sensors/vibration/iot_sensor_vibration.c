#include "iot_sensor_vibration.h"
#include "iot_logger.h"

int iot_vibration_init(const iot_gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("vibration", "init fail: null bus");
        return -1;
    }
    LOG_I("vibration", "init ok pin %u", (unsigned int)pin);
    return 0;
}

int iot_vibration_read(const iot_gpio_bus_t *bus, uint8_t pin, bool *vibrating)
{
    uint8_t level;

    if (bus == NULL || vibrating == NULL) {
        return -1;
    }
    level = iot_gpio_get(bus, pin);
    *vibrating = (level != 0);
    return 0;
}