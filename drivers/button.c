#include "button.h"
#include "hal.h"
#include "ros.h"
#include "common.h"

int button_init(button_t *btn, uint8_t pin, uint8_t active_level,
                    uint32_t debounce_ms, uint32_t long_press_ms,
                    button_callback_fn cb, void *user)
{
    hal_gpio_config_t cfg;

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
    btn->raw_state = 0;
    btn->last_change_ms = 0;
    btn->press_start = 0;
    btn->press_reported = 0;

    /* 输入上拉 */
    cfg.mode = HAL_GPIO_MODE_INPUT;
    cfg.output = HAL_GPIO_OUTPUT_PP;
    cfg.pull = HAL_GPIO_PULLUP;
    cfg.speed = HAL_GPIO_SPEED_LOW;
    cfg.alternate = 0;
    cfg.initial_level = HAL_GPIO_HIGH;
    return (hal_gpio_init(HAL_PIN(0, pin), &cfg) == HAL_OK) ? 0 : -1;
}

int button_process(button_t *btn, uint32_t tick_ms)
{
    hal_gpio_level_e level;
    uint8_t pressed;
    int ev = -1;                 /* -1 表示无事件（BUTTON_RELEASED 是合法事件） */

    if (btn == NULL) {
        return -1;
    }
    if (hal_gpio_read(HAL_PIN(0, btn->pin), &level) != HAL_OK) {
        return -1;
    }
    pressed = ((uint8_t)level == btn->active_level) ? 1 : 0;

    /*
     * 去抖：原始电平变化时重置计时，只有稳定 debounce_ms 之后才更新稳定状态。
     * 原实现直接接受第一个采样，抖动会被当成多次按下。
     */
    if (pressed != btn->raw_state) {
        btn->raw_state = pressed;
        btn->last_change_ms = tick_ms;
    } else if (pressed != btn->stable_state &&
               (uint32_t)(tick_ms - btn->last_change_ms) >= btn->debounce_ms) {
        btn->stable_state = pressed;
        if (pressed) {
            btn->press_start = tick_ms;
            btn->press_reported = 0;
            ev = BUTTON_PRESSED;
        } else {
            ev = BUTTON_RELEASED;
        }
    }

    if (btn->stable_state != 0U) {
        if (!btn->press_reported && btn->long_press_ms > 0U &&
            (uint32_t)(tick_ms - btn->press_start) >= btn->long_press_ms) {
            btn->press_reported = 1;
            btn->callback(BUTTON_LONG_PRESSED, btn->user);
        }
    } else {
        /* 短按：释放时补一次 CLICKED（长按已单独上报） */
        if ((ev == BUTTON_RELEASED) && !btn->press_reported &&
            btn->long_press_ms > 0U) {
            btn->callback(BUTTON_CLICKED, btn->user);
        }
        btn->press_reported = 0;
    }

    if (ev >= 0) {
        btn->callback((button_event_e)ev, btn->user);
    }
    return HAL_OK;
}
