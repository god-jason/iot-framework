#ifndef MOTOR_H
#define MOTOR_H

/*
 * 电机抽象：上层通过统一接口控制方向 / 速度，底层由不同驱动芯片实现。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOTOR_STOPPED,
    MOTOR_FORWARD,
    MOTOR_REVERSE,
    MOTOR_BRAKING
} motor_direction_t;

typedef struct {
    int (*set_direction)(void *ctx, motor_direction_t direction);
    int (*set_speed)(void *ctx, int32_t speed);
    int (*stop)(void *ctx);
    void *ctx;
} motor_ops_t;

typedef struct {
    const motor_ops_t *ops;

    int32_t speed;
    int32_t max_speed;

    motor_direction_t direction;
} motor_t;

int motor_init(motor_t *motor, const motor_ops_t *ops);
int motor_set_speed(motor_t *motor, int32_t speed);
int motor_set_direction(motor_t *motor, motor_direction_t direction);
int motor_stop(motor_t *motor);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_H */