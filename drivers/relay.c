#include "relay.h"
#include "iot.h"
#include "log.h"

int relay_init(relay_t *relay, uint8_t pin, uint8_t active_level)
{
    iot_gpio_config_t cfg;

    if (relay == NULL) {
        return -1;
    }
    relay->pin = pin;
    relay->active_level = active_level;

    cfg.mode = IOT_GPIO_MODE_OUTPUT;
    cfg.output = IOT_GPIO_OUTPUT_PP;
    cfg.pull = IOT_GPIO_NOPULL;
    cfg.speed = IOT_GPIO_SPEED_LOW;
    cfg.alternate = 0;
    cfg.initial_level = (active_level != 0) ? IOT_GPIO_LOW : IOT_GPIO_HIGH;
    if (iot_gpio_init(IOT_PIN(0, pin), &cfg) != IOT_OK) {
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
    return (iot_gpio_write(IOT_PIN(0, relay->pin),
                            (relay->active_level != 0) ? IOT_GPIO_HIGH : IOT_GPIO_LOW) == IOT_OK)
           ? 0 : -1;
}

int relay_off(relay_t *relay)
{
    if (relay == NULL) {
        return -1;
    }
    return (iot_gpio_write(IOT_PIN(0, relay->pin),
                            (relay->active_level != 0) ? IOT_GPIO_LOW : IOT_GPIO_HIGH) == IOT_OK)
           ? 0 : -1;
}

int relay_toggle(relay_t *relay)
{
    if (relay == NULL) {
        return -1;
    }
    LOG_I("relay", "toggle");
    return (iot_gpio_toggle(IOT_PIN(0, relay->pin)) == IOT_OK) ? 0 : -1;
}
