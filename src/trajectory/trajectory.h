#ifndef TRAJECTORY_H
#define TRAJECTORY_H

/*
 * trajectory.h - 一维/多轴轨迹规划（位置、速度、加速度）
 *
 * 提供两种速度曲线：
 *   - 梯形：加速 - 匀速 - 减速，计算简单，适合大多数点位运动；
 *   - S 曲线：加加速度（jerk）受限，起停平滑，适合高动态机构。
 *
 * 采样接口返回给定时刻的位置/速度/加速度，供控制环逐周期调用。
 * 所有对象由调用者静态分配，无动态内存。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 梯形曲线 ============================== */

typedef struct {
    float distance;     /* 总行程（>0） */
    float vmax;         /* 最大速度（>0） */
    float acc;          /* 加速度（>0） */
    float dec;          /* 减速度（>0） */

    float vpeak;        /* 实际达到的峰值速度 */
    float t_acc;        /* 加速段时长 */
    float t_cruise;     /* 匀速段时长 */
    float t_dec;        /* 减速段时长 */
    float t_total;      /* 总时长 */
} trajectory_trap_t;

/*
 * 规划梯形速度曲线（起止速度均为 0）。
 * 行程不足以达到 vmax 时自动降低峰值速度。
 * 返回：0 成功；-1 参数非法。
 */
int trajectory_trap_plan(trajectory_trap_t *t, float distance,
                         float vmax, float acc, float dec);

/*
 * 采样梯形曲线。
 * pos/vel/acc 均可为 NULL；time 超出 [0, t_total] 时按端点处理。
 * 返回：0 成功；-1 参数非法或尚未规划。
 */
int trajectory_trap_sample(const trajectory_trap_t *t, float time,
                           float *pos, float *vel, float *acc);

/* ============================== S 曲线 ============================== */

typedef struct {
    float distance;     /* 总行程（>0） */
    float vmax;         /* 最大速度（>0） */
    float amax;         /* 最大加速度（>0） */
    float jerk;         /* 最大加加速度（>0） */

    float vpeak;        /* 实际峰值速度 */

    float t1;           /* 加速段：jerk 上升时长（减速段镜像） */
    float t2;           /* 加速段：恒定加速度时长 */
    float t3;           /* 加速段：jerk 下降时长 */
    float t_cruise;     /* 匀速段时长 */

    float s_acc;        /* 加速段位移 */
    float t_acc;        /* 加速总时长 */
    float t_total;      /* 总时长 */
} trajectory_scurve_t;

/*
 * 规划 S 曲线（起止速度、加速度均为 0）。
 * 行程不足时自动降低峰值速度；内部用二分法求解，规划耗时可忽略。
 * 返回：0 成功；-1 参数非法。
 */
int trajectory_scurve_plan(trajectory_scurve_t *t, float distance,
                           float vmax, float amax, float jerk);

/*
 * 采样 S 曲线。
 * pos/vel/acc 均可为 NULL；time 超出 [0, t_total] 时按端点处理。
 * 返回：0 成功；-1 参数非法或尚未规划。
 */
int trajectory_scurve_sample(const trajectory_scurve_t *t, float time,
                             float *pos, float *vel, float *acc);

/* ============================== 多轴直线插补 ============================== */

/*
 * 归一化规划：把“起点到终点”映射为单位行程，规划一条通用速度曲线。
 * 返回：0 成功；-1 参数非法。
 */
int trajectory_linear_plan(trajectory_trap_t *t, float vmax,
                           float acc, float dec);

/*
 * 多轴直线插补采样：各轴按同一进度同步运动。
 *   profile   : 由 trajectory_linear_plan 规划的归一化曲线
 *   start/end : 各轴起止坐标，长度 = axis_count
 *   out       : 输出坐标，长度 = axis_count
 * 返回：0 成功；-1 参数非法。
 */
int trajectory_linear_sample(const trajectory_trap_t *profile, float time,
                             const float *start, const float *end,
                             uint8_t axis_count, float *out);

#ifdef __cplusplus
}
#endif

#endif /* TRAJECTORY_H */
