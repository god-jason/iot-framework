#include "iot_drv_relay.h"
#include "iot_mcu.h"
#include "iot_logger.h"

int iot_relay_init(iot_relay_t *relay, uint8_t pin, uint8_t active_level)
{
    if (relay == NULL) {
        return IOT_ERR_PARAM;
    }
    relay->pin = pin;
    relay->active_level = active_level;
    if (iot_mcu_gpio_set_mode(pin, 1) != IOT_OK) {
        return IOT_ERR_IO;
    }
    return iot_relay_off(relay);
}

int iot_relay_on(iot_relay_t *relay)
{
    if (relay == NULL) {
        return IOT_ERR_PARAM;
    }
    LOG_I("relay", "on");
    return iot_mcu_gpio_write(relay->pin, relay->active_level ? 1 : 0);
}

int iot_relay_off(iot_relay_t *relay)
{
    if (relay == NULL) {
        return IOT_ERR_PARAM;
    }
    return iot_mcu_gpio_write(relay->pin, relay->active_level ? 0 : 1);
}

int iot_relay_toggle(iot_relay_t *relay)
{
    if (relay == NULL) {
        return IOT_ERR_PARAM;
    }
    LOG_I("relay", "toggle");
    return iot_mcu_gpio_toggle(relay->pin);
}