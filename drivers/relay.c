#include "relay.h"
#include "iot_mcu.h"
#include "iot_logger.h"

int relay_init(relay_t *relay, uint8_t pin, uint8_t active_level)
{
    if (relay == NULL) {
        return -1;
    }
    relay->pin = pin;
    relay->active_level = active_level;
    if (iot_mcu_gpio_set_mode(pin, 1) != IOT_OK) {
        return -1;
    }
    return relay_off(relay);
}

int relay_on(relay_t *relay)
{
    if (relay == NULL) {
        return -1;
    }
    LOG_I("relay", "on");
    return iot_mcu_gpio_write(relay->pin, relay->active_level ? 1 : 0);
}

int relay_off(relay_t *relay)
{
    if (relay == NULL) {
        return -1;
    }
    return iot_mcu_gpio_write(relay->pin, relay->active_level ? 0 : 1);
}

int relay_toggle(relay_t *relay)
{
    if (relay == NULL) {
        return -1;
    }
    LOG_I("relay", "toggle");
    return iot_mcu_gpio_toggle(relay->pin);
}