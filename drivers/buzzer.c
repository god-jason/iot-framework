#include "buzzer.h"
#include "common/driver_io.h"
#include "log.h"

int buzzer_init(buzzer_t *buzzer, uint8_t pin, uint8_t active_level)
{
    iot_gpio_level_t initial;

    if (buzzer == NULL) {
        return -1;
    }
    buzzer->pin = pin;
    buzzer->active_level = active_level;

    initial = (active_level != 0) ? IOT_GPIO_LOW : IOT_GPIO_HIGH;
    if (driver_gpio_output(IOT_PIN(0, pin), initial) != DRIVER_OK) {
        return -1;
    }
    return buzzer_stop(buzzer);
}

int buzzer_start(buzzer_t *buzzer)
{
    if (buzzer == NULL) {
        return -1;
    }
    LOG_I("buzzer", "beep on");
    return (driver_gpio_write(IOT_PIN(0, buzzer->pin),
                              (buzzer->active_level != 0) ? IOT_GPIO_HIGH : IOT_GPIO_LOW) == DRIVER_OK)
           ? 0 : -1;
}

int buzzer_stop(buzzer_t *buzzer)
{
    if (buzzer == NULL) {
        return -1;
    }
    return (driver_gpio_write(IOT_PIN(0, buzzer->pin),
                              (buzzer->active_level != 0) ? IOT_GPIO_LOW : IOT_GPIO_HIGH) == DRIVER_OK)
           ? 0 : -1;
}

int buzzer_beep(buzzer_t *buzzer, uint32_t ms)
{
    if (buzzer == NULL) {
        return -1;
    }
    buzzer_start(buzzer);
    driver_delay_ms(ms);
    return buzzer_stop(buzzer);
}
