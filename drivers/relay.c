#include "relay.h"
#include "port.h"
#include "log.h"

int relay_init(relay_t *relay, uint8_t pin, uint8_t active_level)
{
    port_gpio_config_t cfg;

    if (relay == NULL) {
        return -1;
    }
    relay->pin = pin;
    relay->active_level = active_level;

    cfg.mode = PORT_GPIO_MODE_OUTPUT;
    cfg.output = PORT_GPIO_OUTPUT_PP;
    cfg.pull = PORT_GPIO_NOPULL;
    cfg.speed = PORT_GPIO_SPEED_LOW;
    cfg.alternate = 0;
    cfg.initial_level = (active_level != 0) ? PORT_GPIO_LOW : PORT_GPIO_HIGH;
    if (port_gpio_init(PORT_PIN(0, pin), &cfg) != PORT_OK) {
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
    return (port_gpio_write(PORT_PIN(0, relay->pin),
                            (relay->active_level != 0) ? PORT_GPIO_HIGH : PORT_GPIO_LOW) == PORT_OK)
           ? 0 : -1;
}

int relay_off(relay_t *relay)
{
    if (relay == NULL) {
        return -1;
    }
    return (port_gpio_write(PORT_PIN(0, relay->pin),
                            (relay->active_level != 0) ? PORT_GPIO_LOW : PORT_GPIO_HIGH) == PORT_OK)
           ? 0 : -1;
}

int relay_toggle(relay_t *relay)
{
    if (relay == NULL) {
        return -1;
    }
    LOG_I("relay", "toggle");
    return (port_gpio_toggle(PORT_PIN(0, relay->pin)) == PORT_OK) ? 0 : -1;
}
