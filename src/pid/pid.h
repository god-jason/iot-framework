#ifndef PID_H
#define PID_H

/*
 * PID 控制（位置式 + 增量式）。算法库独立于 Core 与 HAL。
 *
 * 仅依赖 C 标准头文件，不使用动态内存与硬件接口，可跨平台复用。
 * 位置式输出绝对量；增量式输出累加量，采样周期视为常数。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 位置式 PID ---- */
typedef struct {
    float kp;               /* 比例增益：输出与当前误差成正比 */
    float ki;               /* 积分增益：输出与误差积分成正比，消除稳态误差 */
    float kd;               /* 微分增益：输出与误差变化率成正比，抑制超调 */

    float target;           /* 最近一次设定的目标值（仅记录，便于观测） */
    float measurement;      /* 最近一次的测量值（仅记录，便于观测） */

    float output;           /* 最近一次限幅后的控制输出 */
    float output_min;       /* 输出下限 */
    float output_max;       /* 输出上限 */

    float integral;         /* 误差积分累计量 ∫e dt */
    float last_error;       /* 上一次误差 e[k-1]，用于后向差分求导 */

    bool initialized;       /* 是否已完成首次更新（false 时跳过微分） */
} pid_t;

/*
 * 初始化位置式 PID。
 * 参数：pid 控制器对象；kp/ki/kd 比例、积分、微分增益。
 * 返回：0 成功；-1 表示 pid 为空。
 */
int pid_init(pid_t *pid, float kp, float ki, float kd);

/*
 * 执行一次位置式 PID 计算。
 * 参数：pid 控制器对象；target 目标值；measurement 测量值；
 *       dt 采样周期（秒，必须大于 0）。
 * 返回：限幅后的控制输出；pid 为空返回 0，dt <= 0 返回上次输出。
 * 注意：首次调用微分项为 0；输出限幅不会回写积分项。
 */
float pid_update(pid_t *pid, float target, float measurement, float dt);

/* 复位内部状态（积分、历史误差、输出），不改增益与限幅。 */
void pid_reset(pid_t *pid);

/* 修改三个增益（支持运行中在线整定），不清除积分与历史误差。 */
void pid_set_params(pid_t *pid, float kp, float ki, float kd);

/* 设置输出上下限。 */
void pid_set_limits(pid_t *pid, float min, float max);

/* ---- 增量式 PID（定周期，dt 隐含） ---- */
typedef struct {
    float kp;               /* 比例增益 */
    float ki;               /* 积分增益（每个采样周期的误差权重） */
    float kd;               /* 微分增益（二阶差分权重） */

    float last_error;       /* e[k-1] */
    float prev_error;       /* e[k-2] */

    float output;           /* 累加后的控制输出 */
    float output_min;       /* 输出下限 */
    float output_max;       /* 输出上限 */

    bool initialized;       /* 是否已完成首次更新 */
} pid_incremental_t;

/*
 * 初始化增量式 PID。
 * 参数：pid 控制器对象；kp/ki/kd 比例、积分、微分增益。
 * 返回：0 成功；-1 表示 pid 为空。
 */
int pid_incremental_init(pid_incremental_t *pid, float kp, float ki, float kd);

/*
 * 执行一次增量式 PID 计算。
 * 参数：pid 控制器对象；target 目标值；measurement 测量值。
 * 返回：限幅后的累加输出；pid 为空返回 0。
 * 注意：首次调用只记录误差、不产生增量。
 */
float pid_incremental_update(pid_incremental_t *pid, float target, float measurement);

/* 复位历史误差与输出（增益与限幅保持不变）。 */
void pid_incremental_reset(pid_incremental_t *pid);

/* 设置累加输出的上下限。 */
void pid_incremental_set_limits(pid_incremental_t *pid, float min, float max);

#ifdef __cplusplus
}
#endif

#endif /* PID_H */
