#include "led.h"
#include "iot.h"
#include "log.h"

int led_init(led_t *led, uint8_t pin, uint8_t active_level)
{
    iot_gpio_config_t cfg;

    if (led == NULL) {
        return -1;
    }
    led->pin = pin;
    led->active_level = active_level;
    led->state = 0;

    cfg.mode = IOT_GPIO_MODE_OUTPUT;
    cfg.output = IOT_GPIO_OUTPUT_PP;
    cfg.pull = IOT_GPIO_NOPULL;
    cfg.speed = IOT_GPIO_SPEED_LOW;
    cfg.alternate = 0;
    cfg.initial_level = (active_level != 0) ? IOT_GPIO_LOW : IOT_GPIO_HIGH;
    if (iot_gpio_init(IOT_PIN(0, pin), &cfg) != IOT_OK) {
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
    return (iot_gpio_write(IOT_PIN(0, led->pin),
                            (led->active_level != 0) ? IOT_GPIO_HIGH : IOT_GPIO_LOW) == IOT_OK)
           ? 0 : -1;
}

int led_off(led_t *led)
{
    if (led == NULL) {
        return -1;
    }
    led->state = 0;
    return (iot_gpio_write(IOT_PIN(0, led->pin),
                            (led->active_level != 0) ? IOT_GPIO_LOW : IOT_GPIO_HIGH) == IOT_OK)
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
