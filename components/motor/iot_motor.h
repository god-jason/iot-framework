#ifndef IOT_MOTOR_H
#define IOT_MOTOR_H

/*
 * 电机抽象：上层通过统一接口控制方向 / 速度，底层由不同驱动芯片实现。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_MOTOR_STOPPED,
    IOT_MOTOR_FORWARD,
    IOT_MOTOR_REVERSE,
    IOT_MOTOR_BRAKING
} iot_motor_direction_t;

typedef struct {
    int (*set_direction)(void *ctx, iot_motor_direction_t direction);
    int (*set_speed)(void *ctx, int32_t speed);
    int (*stop)(void *ctx);
    void *ctx;
} iot_motor_ops_t;

typedef struct {
    const iot_motor_ops_t *ops;

    int32_t speed;
    int32_t max_speed;

    iot_motor_direction_t direction;
} iot_motor_t;

int iot_motor_init(iot_motor_t *motor, const iot_motor_ops_t *ops);
int iot_motor_set_speed(iot_motor_t *motor, int32_t speed);
int iot_motor_set_direction(iot_motor_t *motor, iot_motor_direction_t direction);
int iot_motor_stop(iot_motor_t *motor);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MOTOR_H */