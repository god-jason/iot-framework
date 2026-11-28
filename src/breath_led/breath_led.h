#ifndef BREATH_LED_H
#define BREATH_LED_H

/*
 * 呼吸灯：PWM 占空比按正弦曲线渐变，实现「呼吸」效果。
 *
 * 模块说明：
 *   - 占空比统一使用 0..1000 表示 0..100%；
 *   - 底层 PWM 输出通过 breath_led_ops_t 回调注入，便于移植；
 *   - 时间基准由调用者通过 breath_led_update() 传入的 tick 提供。
 * 使用约束：
 *   - tick 必须单调递增，否则相位计算会回跳；
 *   - 相位不做周期取模，一个呼吸周期结束后需重新 start()。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 底层 PWM 操作表 */
typedef struct {
    int (*set_duty)(void *ctx, uint16_t duty);   /* 设置占空比，范围 0..1000 */
    void *ctx;                                   /* 驱动私有上下文 */
} breath_led_ops_t;

/* 呼吸灯对象状态 */
typedef struct {
    const breath_led_ops_t *ops;   /* PWM 操作表 */

    uint16_t duty;                 /* 最近一次输出的占空比（0..1000） */
    uint16_t period_ms;            /* 呼吸周期（毫秒），默认 2000 */
    uint32_t start_tick;           /* 相位零点，首次 update 时锁存 */
    bool started;                  /* 是否已锁存起始 tick */
    bool active;                   /* 是否处于呼吸输出状态 */
} breath_led_t;

/*
 * 初始化呼吸灯。
 * 参数：led 对象；ops PWM 操作表（set_duty 必须非 NULL）。
 * 返回：0 成功；参数非法返回 -1。
 */
int breath_led_init(breath_led_t *led, const breath_led_ops_t *ops);

/*
 * 启动呼吸效果。
 * 参数：led 对象；period_ms 周期（毫秒，必须 > 0）。
 * 返回：0 成功；参数非法返回 -1。
 */
int breath_led_start(breath_led_t *led, uint16_t period_ms);

/* 由主循环周期调用：根据当前 tick 计算正弦占空比并输出 */
int breath_led_update(breath_led_t *led, uint32_t tick_ms);

/*
 * 停止呼吸并熄灭（占空比置 0）。
 * 参数：led 对象。
 * 返回：底层 set_duty 的返回值；led 为 NULL 返回 -1。
 */
int breath_led_stop(breath_led_t *led);

#ifdef __cplusplus
}
#endif

#endif /* BREATH_LED_H */