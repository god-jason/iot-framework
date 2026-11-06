#ifndef IOT_STEPPER_H
#define IOT_STEPPER_H

/*
 * 步进电机：方向 + 步进脉冲控制。底层 ops 由 GPIO/专用驱动实现。
 * 位置以「步数」为单位。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*set_dir)(void *ctx, uint8_t dir);   /* 0=正向 1=反向 */
    int (*step)(void *ctx);                   /* 发出一个脉冲 */
    void *ctx;
} iot_stepper_ops_t;

typedef struct {
    const iot_stepper_ops_t *ops;

    int32_t position;    /* 当前步位置 */
    uint8_t direction;
} iot_stepper_t;

int iot_stepper_init(iot_stepper_t *st, const iot_stepper_ops_t *ops);

int iot_stepper_set_direction(iot_stepper_t *st, uint8_t dir);

/* 相对移动 steps 步（阻塞执行，带符号：正=正向 负=反向） */
int iot_stepper_move(iot_stepper_t *st, int32_t steps);

/* 移动到绝对目标位置 */
int iot_stepper_move_to(iot_stepper_t *st, int32_t target);

int iot_stepper_get_position(iot_stepper_t *st, int32_t *position);

#ifdef __cplusplus
}
#endif

#endif /* IOT_STEPPER_H */