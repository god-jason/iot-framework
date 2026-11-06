#include "iot_sensor_button.h"
#include "iot_mcu.h"
#include "iot_logger.h"

int iot_button_init(iot_button_t *btn, uint8_t pin, uint8_t active_level,
                    uint32_t debounce_ms, uint32_t long_press_ms,
                    iot_button_callback_fn cb, void *user)
{
    if (btn == NULL || cb == NULL) {
        return IOT_ERR_PARAM;
    }
    btn->pin = pin;
    btn->active_level = active_level;
    btn->debounce_ms = debounce_ms;
    btn->long_press_ms = long_press_ms;
    btn->callback = cb;
    btn->user = user;
    btn->stable_state = 0;
    btn->last_state = 0;
    btn->press_start = 0;
    btn->press_reported = 0;
    return iot_mcu_gpio_set_mode(pin, 0);   /* 输入 */
}

int iot_button_process(iot_button_t *btn, uint32_t tick_ms)
{
    uint8_t level, pressed;
    iot_button_event_t ev = IOT_BUTTON_RELEASED;

    if (btn == NULL) {
        return IOT_ERR_PARAM;
    }
    if (iot_mcu_gpio_read(btn->pin, &level) != IOT_OK) {
        return IOT_ERR_IO;
    }
    pressed = (level == btn->active_level) ? 1 : 0;

    /* 状态变化时记录时间戳（简单去抖） */
    if (pressed != btn->stable_state) {
        btn->stable_state = pressed;
        if (pressed) {
            btn->press_start = tick_ms;
            btn->press_reported = 0;
        }
    }

    if (pressed) {
        if (!btn->press_reported &&
            (tick_ms - btn->press_start) >= btn->long_press_ms &&
            btn->long_press_ms > 0) {
            ev = IOT_BUTTON_LONG_PRESSED;
            btn->press_reported = 1;
            LOG_I("btn", "long press");
        }
    } else {
        if (btn->stable_state == 0 && btn->last_state == 1) {
            ev = IOT_BUTTON_CLICKED;
            LOG_I("btn", "clicked");
        }
    }
    btn->last_state = pressed;

    if (ev != IOT_BUTTON_RELEASED) {
        btn->callback(ev, btn->user);
    }
    return IOT_OK;
}