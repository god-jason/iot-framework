#ifndef BUTTON_H
#define BUTTON_H

/*
 * 按键（GPIO 输入 + 软件去抖）。支持按下/释放沿与长按检测。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUTTON_RELEASED,
    BUTTON_PRESSED,
    BUTTON_CLICKED,
    BUTTON_LONG_PRESSED
} button_event_e;

typedef void (*button_callback_fn)(button_event_e event, void *user);

typedef struct {
    uint8_t pin;
    uint8_t active_level;      /* 按下电平 */
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    button_callback_fn callback;
    void *user;

    uint8_t stable_state;
    uint8_t last_state;
    uint32_t press_start;
    uint8_t press_reported;
} button_t;

int button_init(button_t *btn, uint8_t pin, uint8_t active_level,
                    uint32_t debounce_ms, uint32_t long_press_ms,
                    button_callback_fn cb, void *user);

/* 主循环周期性调用（如 10ms）去抖与检测沿 */
int button_process(button_t *btn, uint32_t tick_ms);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H */