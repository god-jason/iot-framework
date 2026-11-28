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

    uint8_t stable_state;      /* 去抖后的稳定状态（1 = 按下） */
    uint8_t raw_state;         /* 最近一次采样值，用于去抖计时 */
    uint32_t last_change_ms;   /* 原始电平最近一次变化时刻 */
    uint32_t press_start;
    uint8_t press_reported;
} button_t;

int button_init(button_t *btn, uint8_t pin, uint8_t active_level,
                    uint32_t debounce_ms, uint32_t long_press_ms,
                    button_callback_fn cb, void *user);

/*
 * 主循环周期性调用（建议 10ms）做去抖与事件检测。
 * 事件顺序：按下沿 BUTTON_PRESSED；持续按住超长按阈值 BUTTON_LONG_PRESSED（一次）；
 * 释放沿 BUTTON_RELEASED；若释放时未触发长按，再补一次 BUTTON_CLICKED。
 */
int button_process(button_t *btn, uint32_t tick_ms);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H */