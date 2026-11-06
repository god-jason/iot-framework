#include "iot_drv_led.h"
#include "iot_mcu.h"
#include "iot_logger.h"

int iot_led_init(iot_led_t *led, uint8_t pin, uint8_t active_level)
{
    if (led == NULL) {
        return IOT_ERR_PARAM;
    }
    led->pin = pin;
    led->active_level = active_level;
    led->state = 0;
    if (iot_mcu_gpio_set_mode(pin, 1) != IOT_OK) {
        return IOT_ERR_IO;
    }
    return iot_led_off(led);
}

int iot_led_on(iot_led_t *led)
{
    if (led == NULL) {
        return IOT_ERR_PARAM;
    }
    led->state = 1;
    return iot_mcu_gpio_write(led->pin, led->active_level ? 1 : 0);
}

int iot_led_off(iot_led_t *led)
{
    if (led == NULL) {
        return IOT_ERR_PARAM;
    }
    led->state = 0;
    return iot_mcu_gpio_write(led->pin, led->active_level ? 0 : 1);
}

int iot_led_toggle(iot_led_t *led)
{
    if (led == NULL) {
        return IOT_ERR_PARAM;
    }
    LOG_I("led", "toggle");   /* toggle led */
    return (led->state) ? iot_led_off(led) : iot_led_on(led);
}