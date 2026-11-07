#ifndef SERVO_H
#define SERVO_H

/*
 * 舵机（PWM 角度控制）。角度映射到脉冲宽度（典型 500..2500us 对应 0..180°）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*set_pulse)(void *ctx, uint16_t us);   /* 设置 PWM 脉宽(us) */
    void *ctx;
} servo_ops_t;

typedef struct {
    const servo_ops_t *ops;

    uint16_t min_pulse_us;
    uint16_t max_pulse_us;
    uint16_t min_angle;
    uint16_t max_angle;

    uint16_t angle;
} servo_t;

int servo_init(servo_t *servo, const servo_ops_t *ops,
                   uint16_t min_us, uint16_t max_us,
                   uint16_t min_angle, uint16_t max_angle);

/* 设置角度（自动换算脉宽） */
int servo_set_angle(servo_t *servo, uint16_t angle);

/* 直接设置脉宽(us) */
int servo_set_pulse(servo_t *servo, uint16_t us);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_H */