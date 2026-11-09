#ifndef DRIVE_H
#define DRIVE_H

/*
 * drive.h - 闭环伺服驱动（位置环 + 速度环）
 *
 * 组合编码器反馈与两级 PID：
 *   位置环输出速度指令 -> 速度环输出执行量（PWM/电流/电压）。
 * 反馈与输出通过 drive_ops_t 注入，便于适配直流电机、步进闭环、舵机等。
 *
 * 单位约定：位置/速度由使用方定义（推荐米、米/秒或弧度、弧度/秒）。
 * 无动态内存，对象由调用者静态分配。
 */

#include <stdbool.h>
#include "pid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 底层反馈/输出操作表 */
typedef struct {
    /* 读取当前位置；成功返回 0 并写入 *pos */
    int (*read_position)(void *ctx, float *pos);
    /* 输出控制量（如 PWM 占空比 -1..1、电流指令等）；成功返回 0 */
    int (*write_output)(void *ctx, float out);
    void *ctx;              /* 驱动私有上下文 */
} drive_ops_t;

typedef struct {
    const drive_ops_t *ops;

    pid_t pos_pid;          /* 位置环 */
    pid_t vel_pid;          /* 速度环（use_velocity_loop 为 true 时生效） */
    bool use_velocity_loop;

    float target;           /* 目标位置 */

    float position;         /* 最近一次反馈位置 */
    float velocity;         /* 由位置差分得到的速度 */
    float last_position;

    float vel_limit;        /* 速度指令限幅（正值） */
    float out_min;          /* 输出下限 */
    float out_max;          /* 输出上限 */
    float output;           /* 最近一次输出 */

    bool primed;            /* 是否已建立位置基准 */
} drive_t;

/*
 * 初始化闭环驱动。
 * 返回：0 成功；-1 参数非法（ops/回调为空）。
 */
int drive_init(drive_t *drive, const drive_ops_t *ops,
               float kp_pos, float ki_pos, float kd_pos,
               float kp_vel, float ki_vel, float kd_vel);

/* 设置目标位置（自动钳位到位置环输出限幅由 PID 负责） */
int drive_set_target(drive_t *drive, float target);

/* 设置速度指令限幅与输出限幅；min 必须小于 max */
int drive_set_limits(drive_t *drive, float vel_limit,
                     float out_min, float out_max);

/* 重置内部状态并建立新位置基准 */
int drive_reset(drive_t *drive, float position);

/*
 * 执行一次闭环更新（建议固定周期调用）。
 * dt 为采样周期（秒，必须 > 0）。
 * 返回：最近一次输出；参数非法或反馈失败时返回上次输出。
 */
float drive_update(drive_t *drive, float dt);

/* 读取最近一次反馈位置 */
int drive_position(const drive_t *drive, float *pos);

#ifdef __cplusplus
}
#endif

#endif /* DRIVE_H */
