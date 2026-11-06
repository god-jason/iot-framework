#ifndef IOT_SENSOR_BUTTON_H
#define IOT_SENSOR_BUTTON_H

/*
 * 按键（GPIO 输入 + 软件去抖）。支持按下/释放沿与长按检测。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_BUTTON_RELEASED,
    IOT_BUTTON_PRESSED,
    IOT_BUTTON_CLICKED,
    IOT_BUTTON_LONG_PRESSED
} iot_button_event_t;

typedef void (*iot_button_callback_fn)(iot_button_event_t event, void *user);

typedef struct {
    uint8_t pin;
    uint8_t active_level;      /* 按下电平 */
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    iot_button_callback_fn callback;
    void *user;

    uint8_t stable_state;
    uint8_t last_state;
    uint32_t press_start;
    uint8_t press_reported;
} iot_button_t;

int iot_button_init(iot_button_t *btn, uint8_t pin, uint8_t active_level,
                    uint32_t debounce_ms, uint32_t long_press_ms,
                    iot_button_callback_fn cb, void *user);

/* 主循环周期性调用（如 10ms）去抖与检测沿 */
int iot_button_process(iot_button_t *btn, uint32_t tick_ms);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SENSOR_BUTTON_H */