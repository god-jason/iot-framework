#include "led.h"
#include "port.h"
#include "iot_logger.h"

int led_init(led_t *led, uint8_t pin, uint8_t active_level)
{
    port_gpio_config_t cfg;

    if (led == NULL) {
        return -1;
    }
    led->pin = pin;
    led->active_level = active_level;
    led->state = 0;

    cfg.mode = PORT_GPIO_MODE_OUTPUT;
    cfg.output = PORT_GPIO_OUTPUT_PP;
    cfg.pull = PORT_GPIO_NOPULL;
    cfg.speed = PORT_GPIO_SPEED_LOW;
    cfg.alternate = 0;
    cfg.initial_level = (active_level != 0) ? PORT_GPIO_LOW : PORT_GPIO_HIGH;
    if (port_gpio_init(PORT_PIN(0, pin), &cfg) != PORT_OK) {
        return -1;
    }
    return led_off(led);
}

int led_on(led_t *led)
{
    if (led == NULL) {
        return -1;
    }
    led->state = 1;
    return (port_gpio_write(PORT_PIN(0, led->pin),
                            (led->active_level != 0) ? PORT_GPIO_HIGH : PORT_GPIO_LOW) == PORT_OK)
           ? 0 : -1;
}

int led_off(led_t *led)
{
    if (led == NULL) {
        return -1;
    }
    led->state = 0;
    return (port_gpio_write(PORT_PIN(0, led->pin),
                            (led->active_level != 0) ? PORT_GPIO_LOW : PORT_GPIO_HIGH) == PORT_OK)
           ? 0 : -1;
}

int led_toggle(led_t *led)
{
    if (led == NULL) {
        return -1;
    }
    LOG_I("led", "toggle");   /* toggle led */
    return (led->state) ? led_off(led) : led_on(led);
}
