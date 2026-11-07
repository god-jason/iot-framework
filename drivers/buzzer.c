#include "buzzer.h"
#include "port.h"
#include "log.h"

int buzzer_init(buzzer_t *buzzer, uint8_t pin, uint8_t active_level)
{
    port_gpio_config_t cfg;

    if (buzzer == NULL) {
        return -1;
    }
    buzzer->pin = pin;
    buzzer->active_level = active_level;

    cfg.mode = PORT_GPIO_MODE_OUTPUT;
    cfg.output = PORT_GPIO_OUTPUT_PP;
    cfg.pull = PORT_GPIO_NOPULL;
    cfg.speed = PORT_GPIO_SPEED_LOW;
    cfg.alternate = 0;
    cfg.initial_level = (active_level != 0) ? PORT_GPIO_LOW : PORT_GPIO_HIGH;
    if (port_gpio_init(PORT_PIN(0, pin), &cfg) != PORT_OK) {
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
    return (port_gpio_write(PORT_PIN(0, buzzer->pin),
                            (buzzer->active_level != 0) ? PORT_GPIO_HIGH : PORT_GPIO_LOW) == PORT_OK)
           ? 0 : -1;
}

int buzzer_stop(buzzer_t *buzzer)
{
    if (buzzer == NULL) {
        return -1;
    }
    return (port_gpio_write(PORT_PIN(0, buzzer->pin),
                            (buzzer->active_level != 0) ? PORT_GPIO_LOW : PORT_GPIO_HIGH) == PORT_OK)
           ? 0 : -1;
}

int buzzer_beep(buzzer_t *buzzer, uint32_t ms)
{
    if (buzzer == NULL) {
        return -1;
    }
    buzzer_start(buzzer);
    port_delay_ms(ms);
    return buzzer_stop(buzzer);
}
