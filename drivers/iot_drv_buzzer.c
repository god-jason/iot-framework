#include "iot_drv_buzzer.h"
#include "iot_mcu.h"
#include "iot_logger.h"

int iot_buzzer_init(iot_buzzer_t *buzzer, uint8_t pin, uint8_t active_level)
{
    if (buzzer == NULL) {
        return IOT_ERR_PARAM;
    }
    buzzer->pin = pin;
    buzzer->active_level = active_level;
    if (iot_mcu_gpio_set_mode(pin, 1) != IOT_OK) {
        return IOT_ERR_IO;
    }
    return iot_buzzer_stop(buzzer);
}

int iot_buzzer_start(iot_buzzer_t *buzzer)
{
    if (buzzer == NULL) {
        return IOT_ERR_PARAM;
    }
    LOG_I("buzzer", "beep on");
    return iot_mcu_gpio_write(buzzer->pin, buzzer->active_level ? 1 : 0);
}

int iot_buzzer_stop(iot_buzzer_t *buzzer)
{
    if (buzzer == NULL) {
        return IOT_ERR_PARAM;
    }
    return iot_mcu_gpio_write(buzzer->pin, buzzer->active_level ? 0 : 1);
}

int iot_buzzer_beep(iot_buzzer_t *buzzer, uint32_t ms)
{
    uint32_t start;

    if (buzzer == NULL) {
        return IOT_ERR_PARAM;
    }
    iot_mcu_timer_init();
    start = iot_mcu_tick_ms();
    iot_buzzer_start(buzzer);
    while ((iot_mcu_tick_ms() - start) < ms) {
        ;   /* host: 忙等模拟鸣叫时长 */
    }
    return iot_buzzer_stop(buzzer);
}