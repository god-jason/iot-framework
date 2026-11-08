#include "relay.h"
#include "hal.h"
#include "log.h"

int relay_init(relay_t *relay, uint8_t pin, uint8_t active_level)
{
    hal_gpio_config_t cfg;

    if (relay == NULL) {
        return -1;
    }
    relay->pin = pin;
    relay->active_level = active_level;

    cfg.mode = HAL_GPIO_MODE_OUTPUT;
    cfg.output = HAL_GPIO_OUTPUT_PP;
    cfg.pull = HAL_GPIO_NOPULL;
    cfg.speed = HAL_GPIO_SPEED_LOW;
    cfg.alternate = 0;
    cfg.initial_level = (active_level != 0) ? HAL_GPIO_LOW : HAL_GPIO_HIGH;
    if (hal_gpio_init(HAL_PIN(0, pin), &cfg) != HAL_OK) {
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
    return (hal_gpio_write(HAL_PIN(0, relay->pin),
                            (relay->active_level != 0) ? HAL_GPIO_HIGH : HAL_GPIO_LOW) == HAL_OK)
           ? 0 : -1;
}

int relay_off(relay_t *relay)
{
    if (relay == NULL) {
        return -1;
    }
    return (hal_gpio_write(HAL_PIN(0, relay->pin),
                            (relay->active_level != 0) ? HAL_GPIO_LOW : HAL_GPIO_HIGH) == HAL_OK)
           ? 0 : -1;
}

int relay_toggle(relay_t *relay)
{
    if (relay == NULL) {
        return -1;
    }
    LOG_I("relay", "toggle");
    return (hal_gpio_toggle(HAL_PIN(0, relay->pin)) == HAL_OK) ? 0 : -1;
}
