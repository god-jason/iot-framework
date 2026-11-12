#include "buzzer.h"
#include "hal.h"
#include "log.h"

int buzzer_init(buzzer_t *buzzer, uint8_t pin, uint8_t active_level)
{
    hal_gpio_level_e initial;

    if (buzzer == NULL) {
        return -1;
    }
    buzzer->pin = pin;
    buzzer->active_level = active_level;

    initial = (active_level != 0) ? HAL_GPIO_LOW : HAL_GPIO_HIGH;
    if (hal_gpio_config_output(HAL_PIN(0, pin), initial) != HAL_OK) {
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
    return (hal_gpio_write(HAL_PIN(0, buzzer->pin),
                              (buzzer->active_level != 0) ? HAL_GPIO_HIGH : HAL_GPIO_LOW) == HAL_OK)
           ? 0 : -1;
}

int buzzer_stop(buzzer_t *buzzer)
{
    if (buzzer == NULL) {
        return -1;
    }
    return (hal_gpio_write(HAL_PIN(0, buzzer->pin),
                              (buzzer->active_level != 0) ? HAL_GPIO_LOW : HAL_GPIO_HIGH) == HAL_OK)
           ? 0 : -1;
}

int buzzer_beep(buzzer_t *buzzer, uint32_t ms)
{
    if (buzzer == NULL) {
        return -1;
    }
    buzzer_start(buzzer);
    hal_delay_ms(ms);
    return buzzer_stop(buzzer);
}
