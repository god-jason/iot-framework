#ifndef IOT_SERVO_H
#define IOT_SERVO_H

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
} iot_servo_ops_t;

typedef struct {
    const iot_servo_ops_t *ops;

    uint16_t min_pulse_us;
    uint16_t max_pulse_us;
    uint16_t min_angle;
    uint16_t max_angle;

    uint16_t angle;
} iot_servo_t;

int iot_servo_init(iot_servo_t *servo, const iot_servo_ops_t *ops,
                   uint16_t min_us, uint16_t max_us,
                   uint16_t min_angle, uint16_t max_angle);

/* 设置角度（自动换算脉宽） */
int iot_servo_set_angle(iot_servo_t *servo, uint16_t angle);

/* 直接设置脉宽(us) */
int iot_servo_set_pulse(iot_servo_t *servo, uint16_t us);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SERVO_H */