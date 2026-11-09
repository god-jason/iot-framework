/*
 * PID 控制算法实现（位置式 + 增量式）。
 *
 * 位置式：u = Kp*e + Ki*∫e dt + Kd*de/dt，积分项显式累加，输出为绝对量；
 * 增量式：Δu = Kp*(e[k]-e[k-1]) + Ki*e[k] + Kd*(e[k]-2e[k-1]+e[k-2])，
 *         输出为逐拍累加量，适合执行机构自身带保持能力的场合。
 *
 * 数据流：调用方按周期送入目标值 target、测量值 measurement 及采样周期
 *         dt（单位秒），模块完成误差计算、积分/微分运算与输出限幅。
 * 边界：pid == NULL 时各接口安全返回；dt <= 0 时不更新状态、返回上次输出；
 *       首次调用不做微分运算，避免目标阶跃带来的微分冲击。
 */
#include "pid.h"

#include <stddef.h>   /* NULL：交叉 C 库不会经其它头间接提供 */

/* 将 x 限制在 [min, max] 区间内，用于输出限幅 */
static float clampf(float x, float min, float max)
{
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

/* ---- 位置式 PID ---- */

/*
 * 初始化位置式 PID 控制器。
 * 参数：pid 控制器对象；kp/ki/kd 比例/积分/微分增益。
 * 返回：0 成功；-1 表示 pid 为空。
 * 说明：输出限幅默认 ±1000000（近似不限幅），内部状态由 pid_reset 清零。
 */
int pid_init(pid_t *pid, float kp, float ki, float kd)
{
    if (pid == NULL) {
        return -1;
    }
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->output_min = -1000000.0f;
    pid->output_max = 1000000.0f;
    pid_reset(pid);
    return 0;
}

/* 复位位置式 PID 的内部状态（积分、历史误差、输出），增益与限幅保持不变。 */
void pid_reset(pid_t *pid)
{
    if (pid == NULL) {
        return;
    }
    pid->integral = 0.0f;
    pid->last_error = 0.0f;
    pid->output = 0.0f;
    pid->initialized = false;
}

/* 修改位置式 PID 的三个增益（支持运行中在线整定），不清除积分与历史误差。 */
void pid_set_params(pid_t *pid, float kp, float ki, float kd)
{
    if (pid == NULL) {
        return;
    }
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

/* 设置位置式 PID 的输出上下限（例如对应 PWM 占空比或电压范围）。 */
void pid_set_limits(pid_t *pid, float min, float max)
{
    if (pid == NULL) {
        return;
    }
    pid->output_min = min;
    pid->output_max = max;
}

/*
 * 位置式 PID 单步计算。
 * 参数：pid 控制器；target 目标值；measurement 测量值；dt 采样周期（秒，需 > 0）。
 * 返回：限幅后的控制输出；pid 为空返回 0，dt <= 0 返回上次输出且不更新状态。
 * 注意：积分项按 error*dt 累加，输出限幅后不回写积分项；
 *       若需抗积分饱和（clamping / back-calculation）需在上层另行处理。
 */
float pid_update(pid_t *pid, float target, float measurement, float dt)
{
    float error, derivative;

    if (pid == NULL || dt <= 0.0f) {
        return (pid == NULL) ? 0.0f : pid->output;
    }
    pid->target = target;
    pid->measurement = measurement;

    /* 误差 e = 目标值 - 测量值（正偏差驱动输出增大） */
    error = target - measurement;
    if (pid->initialized) {
        /* 积分项累加 e*dt；微分项取后向差分 (e[k]-e[k-1])/dt */
        pid->integral += error * dt;
        derivative = (error - pid->last_error) / dt;
    } else {
        /* 首次调用无历史误差，微分项置 0，避免冲击 */
        derivative = 0.0f;
        pid->initialized = true;
    }

    /* 三项求和并按输出限幅，随后保存本次误差供下次微分使用 */
    pid->output = pid->kp * error + pid->ki * pid->integral + pid->kd * derivative;
    pid->output = clampf(pid->output, pid->output_min, pid->output_max);
    pid->last_error = error;
    return pid->output;
}

/* ---- 增量式 PID ---- */

/*
 * 初始化增量式 PID 控制器。
 * 参数：pid 控制器对象；kp/ki/kd 比例/积分/微分增益。
 * 返回：0 成功；-1 表示 pid 为空。
 */
int pid_incremental_init(pid_incremental_t *pid, float kp, float ki, float kd)
{
    if (pid == NULL) {
        return -1;
    }
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->output_min = -1000000.0f;
    pid->output_max = 1000000.0f;
    pid_incremental_reset(pid);
    return 0;
}

/* 复位增量式 PID 的历史误差与输出（重新从当前误差开始跟踪）。 */
void pid_incremental_reset(pid_incremental_t *pid)
{
    if (pid == NULL) {
        return;
    }
    pid->last_error = 0.0f;
    pid->prev_error = 0.0f;
    pid->output = 0.0f;
    pid->initialized = false;
}

/* 设置增量式 PID 累加输出的上下限。 */
void pid_incremental_set_limits(pid_incremental_t *pid, float min, float max)
{
    if (pid == NULL) {
        return;
    }
    pid->output_min = min;
    pid->output_max = max;
}

/*
 * 增量式 PID 单步计算（采样周期固定，dt 隐含在 Ki/Kd 中）。
 * 参数：pid 控制器；target 目标值；measurement 测量值。
 * 返回：限幅后的累加输出；pid 为空返回 0。
 * 注意：首次调用只记录误差并返回当前输出，不产生增量。
 */
float pid_incremental_update(pid_incremental_t *pid, float target, float measurement)
{
    float error, delta;

    if (pid == NULL) {
        return 0.0f;
    }
    error = target - measurement;

    if (!pid->initialized) {
        /* 首次：用当前误差初始化历史误差，增量保持 0 */
        pid->last_error = error;
        pid->prev_error = error;
        pid->initialized = true;
        return pid->output;
    }

    /* Δu = Kp*(e[k]-e[k-1]) + Ki*e[k] + Kd*(e[k]-2e[k-1]+e[k-2]) */
    delta = pid->kp * (error - pid->last_error) +
            pid->ki * error +
            pid->kd * (error - 2.0f * pid->last_error + pid->prev_error);

    /* 增量累加到输出并限幅，再滚动保存误差历史 */
    pid->output += delta;
    pid->output = clampf(pid->output, pid->output_min, pid->output_max);

    pid->prev_error = pid->last_error;
    pid->last_error = error;
    return pid->output;
}
