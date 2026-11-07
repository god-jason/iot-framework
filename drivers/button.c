#include "button.h"
#include "iot_mcu.h"
#include "iot_logger.h"

int button_init(button_t *btn, uint8_t pin, uint8_t active_level,
                    uint32_t debounce_ms, uint32_t long_press_ms,
                    button_callback_fn cb, void *user)
{
    if (btn == NULL || cb == NULL) {
        return -1;
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

int button_process(button_t *btn, uint32_t tick_ms)
{
    uint8_t level, pressed;
    button_event_t ev = BUTTON_RELEASED;

    if (btn == NULL) {
        return -1;
    }
    if (iot_mcu_gpio_read(btn->pin, &level) != IOT_OK) {
        return -1;
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
            ev = BUTTON_LONG_PRESSED;
            btn->press_reported = 1;
            LOG_I("btn", "long press");
        }
    } else {
        if (btn->stable_state == 0 && btn->last_state == 1) {
            ev = BUTTON_CLICKED;
            LOG_I("btn", "clicked");
        }
    }
    btn->last_state = pressed;

    if (ev != BUTTON_RELEASED) {
        btn->callback(ev, btn->user);
    }
    return IOT_OK;
}