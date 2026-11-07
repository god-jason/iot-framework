#include "led.h"
#include "iot_mcu.h"
#include "iot_logger.h"

int led_init(led_t *led, uint8_t pin, uint8_t active_level)
{
    if (led == NULL) {
        return -1;
    }
    led->pin = pin;
    led->active_level = active_level;
    led->state = 0;
    if (iot_mcu_gpio_set_mode(pin, 1) != IOT_OK) {
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
    return iot_mcu_gpio_write(led->pin, led->active_level ? 1 : 0);
}

int led_off(led_t *led)
{
    if (led == NULL) {
        return -1;
    }
    led->state = 0;
    return iot_mcu_gpio_write(led->pin, led->active_level ? 0 : 1);
}

int led_toggle(led_t *led)
{
    if (led == NULL) {
        return -1;
    }
    LOG_I("led", "toggle");   /* toggle led */
    return (led->state) ? led_off(led) : led_on(led);
}