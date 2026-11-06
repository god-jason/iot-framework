#ifndef IOT_BREATH_LED_H
#define IOT_BREATH_LED_H

/*
 * 呼吸灯：PWM 占空比按正弦曲线渐变，实现「呼吸」效果。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*set_duty)(void *ctx, uint16_t duty);
    void *ctx;
} iot_breath_led_ops_t;

typedef struct {
    const iot_breath_led_ops_t *ops;

    uint16_t duty;
    uint16_t period_ms;    /* 呼吸周期 */
    uint32_t start_tick;
    bool started;
    bool active;
} iot_breath_led_t;

int iot_breath_led_init(iot_breath_led_t *led, const iot_breath_led_ops_t *ops);
int iot_breath_led_start(iot_breath_led_t *led, uint16_t period_ms);

/* 由主循环周期调用：根据当前 tick 计算正弦占空比 */
int iot_breath_led_update(iot_breath_led_t *led, uint32_t tick_ms);

int iot_breath_led_stop(iot_breath_led_t *led);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BREATH_LED_H */