#include "led.h"
#include "hal.h"
#include "ros.h"
#include "common.h"

int led_init(led_t *led, uint8_t pin, uint8_t active_level)
{
    hal_gpio_config_t cfg;

    if (led == NULL) {
        return -1;
    }
    led->pin = pin;
    led->active_level = active_level;
    led->state = 0;

    cfg.mode = HAL_GPIO_MODE_OUTPUT;
    cfg.output = HAL_GPIO_OUTPUT_PP;
    cfg.pull = HAL_GPIO_NOPULL;
    cfg.speed = HAL_GPIO_SPEED_LOW;
    cfg.alternate = 0;
    cfg.initial_level = (active_level != 0) ? HAL_GPIO_LOW : HAL_GPIO_HIGH;
    if (hal_gpio_init(HAL_PIN(0, pin), &cfg) != HAL_OK) {
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
    return (hal_gpio_write(HAL_PIN(0, led->pin),
                            (led->active_level != 0) ? HAL_GPIO_HIGH : HAL_GPIO_LOW) == HAL_OK)
           ? 0 : -1;
}

int led_off(led_t *led)
{
    if (led == NULL) {
        return -1;
    }
    led->state = 0;
    return (hal_gpio_write(HAL_PIN(0, led->pin),
                            (led->active_level != 0) ? HAL_GPIO_LOW : HAL_GPIO_HIGH) == HAL_OK)
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
