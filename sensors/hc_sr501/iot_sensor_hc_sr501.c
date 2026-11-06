#include "iot_sensor_hc_sr501.h"
#include "iot_logger.h"

int iot_pir_init(const iot_gpio_bus_t *bus, uint8_t pin)
{
    if (bus == NULL) {
        LOG_E("pir", "init fail: null bus");
        return -1;
    }
    LOG_I("pir", "init ok pin %u", (unsigned int)pin);
    return 0;
}

int iot_pir_read(const iot_gpio_bus_t *bus, uint8_t pin, bool *motion)
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