#ifndef SERVO_H
#define SERVO_H

/*
 * 舵机（PWM 角度控制）。角度映射到脉冲宽度（典型 500..2500us 对应 0..180°）。
 *
 * 模块说明：
 *   - 角度与脉宽为线性映射，参数在 init 时一次性配置；
 *   - 底层输出通过 servo_ops_t 注入，模块不直接操作定时器。
 * 使用约束：
 *   - 必须满足 max_pulse_us > min_pulse_us 且 max_angle > min_angle；
 *   - set_angle 会自动钳位角度，set_pulse 则原样下发不做校验。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 底层 PWM 操作表 */
typedef struct {
    int (*set_pulse)(void *ctx, uint16_t us);   /* 设置 PWM 脉宽(us)，必须实现 */
    void *ctx;                                  /* 驱动私有上下文 */
} servo_ops_t;

/* 舵机对象：保存映射参数与最近一次角度 */
typedef struct {
    const servo_ops_t *ops;   /* 底层操作表 */

    uint16_t min_pulse_us;    /* 最小角度对应脉宽（微秒） */
    uint16_t max_pulse_us;    /* 最大角度对应脉宽（微秒） */
    uint16_t min_angle;       /* 机械最小角度（度） */
    uint16_t max_angle;       /* 机械最大角度（度） */

    uint16_t angle;           /* 最近一次设置的角度 */
} servo_t;

/*
 * 初始化舵机。
 * 参数：servo 对象；ops 操作表（set_pulse 必须非 NULL）；
 *       min_us/max_us 脉宽范围；min_angle/max_angle 角度范围。
 * 返回：0 成功；参数非法返回 -1。
 */
int servo_init(servo_t *servo, const servo_ops_t *ops,
                   uint16_t min_us, uint16_t max_us,
                   uint16_t min_angle, uint16_t max_angle);

/* 设置角度（自动换算脉宽并钳位） */
int servo_set_angle(servo_t *servo, uint16_t angle);

/* 直接设置脉宽(us)，不做范围校验 */
int servo_set_pulse(servo_t *servo, uint16_t us);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_H */