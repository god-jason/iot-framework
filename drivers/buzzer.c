#include "buzzer.h"
#include "iot_mcu.h"
#include "iot_logger.h"

int buzzer_init(buzzer_t *buzzer, uint8_t pin, uint8_t active_level)
{
    if (buzzer == NULL) {
        return -1;
    }
    buzzer->pin = pin;
    buzzer->active_level = active_level;
    if (iot_mcu_gpio_set_mode(pin, 1) != IOT_OK) {
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
    return iot_mcu_gpio_write(buzzer->pin, buzzer->active_level ? 1 : 0);
}

int buzzer_stop(buzzer_t *buzzer)
{
    if (buzzer == NULL) {
        return -1;
    }
    return iot_mcu_gpio_write(buzzer->pin, buzzer->active_level ? 0 : 1);
}

int buzzer_beep(buzzer_t *buzzer, uint32_t ms)
{
    uint32_t start;

    if (buzzer == NULL) {
        return -1;
    }
    iot_mcu_timer_init();
    start = iot_mcu_tick_ms();
    buzzer_start(buzzer);
    while ((iot_mcu_tick_ms() - start) < ms) {
        ;   /* host: 忙等模拟鸣叫时长 */
    }
    return buzzer_stop(buzzer);
}