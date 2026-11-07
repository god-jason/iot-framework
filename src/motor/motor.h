#ifndef MOTOR_H
#define MOTOR_H

/*
 * 电机抽象：上层通过统一接口控制方向 / 速度，底层由不同驱动芯片实现。
 *
 * 模块说明：
 *   - 速度带符号：正值表示正转、负值表示反转，绝对值不超过 max_speed；
 *   - direction 为软件侧记录的状态，实际换向仍由底层驱动完成；
 *   - ops 中除 ctx 外的回调均可为 NULL，缺失时对应操作返回 -1。
 * 使用约束：
 *   - max_speed 默认 1000，可在初始化后直接修改结构体字段；
 *   - 本模块不做加减速曲线，需要时由上层（如 motion）生成速度序列。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 电机运行方向 / 状态 */
typedef enum {
    MOTOR_STOPPED,   /* 停止（自由停车） */
    MOTOR_FORWARD,   /* 正转 */
    MOTOR_REVERSE,   /* 反转 */
    MOTOR_BRAKING    /* 制动 */
} motor_direction_t;

/* 底层电机驱动操作表 */
typedef struct {
    int (*set_direction)(void *ctx, motor_direction_t direction);   /* 设置方向，可选 */
    int (*set_speed)(void *ctx, int32_t speed);                     /* 设置带符号速度，可选 */
    int (*stop)(void *ctx);                                         /* 停止，可选 */
    void *ctx;                                                      /* 驱动私有上下文 */
} motor_ops_t;

/* 电机对象状态 */
typedef struct {
    const motor_ops_t *ops;   /* 底层操作表 */

    int32_t speed;            /* 当前速度（带符号） */
    int32_t max_speed;        /* 速度限幅上限，默认 1000 */

    motor_direction_t direction;   /* 当前方向/状态记录 */
} motor_t;

/*
 * 初始化电机对象（默认速度 0、最大速度 1000、状态停止）。
 * 参数：motor 对象；ops 操作表。
 * 返回：0 成功；参数非法返回 -1。
 */
int motor_init(motor_t *motor, const motor_ops_t *ops);

/*
 * 设置速度（带符号，按 ±max_speed 钳位）。
 * 参数：motor 对象；speed 目标速度。
 * 返回：0 成功；参数非法或底层失败返回 -1。
 */
int motor_set_speed(motor_t *motor, int32_t speed);

/*
 * 设置方向（不改变速度）。
 * 参数：motor 对象；direction 目标方向。
 * 返回：0 成功；参数非法或底层未实现/失败返回 -1。
 */
int motor_set_direction(motor_t *motor, motor_direction_t direction);

/*
 * 停止电机并复位软件状态。
 * 参数：motor 对象。
 * 返回：0 成功；参数非法返回 -1。
 */
int motor_stop(motor_t *motor);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_H */