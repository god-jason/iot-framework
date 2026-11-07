#include "pid.h"

static float clampf(float x, float min, float max)
{
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

/* ---- 位置式 PID ---- */
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

void pid_set_params(pid_t *pid, float kp, float ki, float kd)
{
    if (pid == NULL) {
        return;
    }
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void pid_set_limits(pid_t *pid, float min, float max)
{
    if (pid == NULL) {
        return;
    }
    pid->output_min = min;
    pid->output_max = max;
}

float pid_update(pid_t *pid, float target, float measurement, float dt)
{
    float error, derivative;

    if (pid == NULL || dt <= 0.0f) {
        return (pid == NULL) ? 0.0f : pid->output;
    }
    pid->target = target;
    pid->measurement = measurement;

    error = target - measurement;
    if (pid->initialized) {
        pid->integral += error * dt;
        derivative = (error - pid->last_error) / dt;
    } else {
        derivative = 0.0f;
        pid->initialized = true;
    }

    pid->output = pid->kp * error + pid->ki * pid->integral + pid->kd * derivative;
    pid->output = clampf(pid->output, pid->output_min, pid->output_max);
    pid->last_error = error;
    return pid->output;
}

/* ---- 增量式 PID ---- */
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

void pid_incremental_set_limits(pid_incremental_t *pid, float min, float max)
{
    if (pid == NULL) {
        return;
    }
    pid->output_min = min;
    pid->output_max = max;
}

float pid_incremental_update(pid_incremental_t *pid, float target, float measurement)
{
    float error, delta;

    if (pid == NULL) {
        return 0.0f;
    }
    error = target - measurement;

    if (!pid->initialized) {
        pid->last_error = error;
        pid->prev_error = error;
        pid->initialized = true;
        return pid->output;
    }

    /* Δu = Kp*(e[k]-e[k-1]) + Ki*e[k] + Kd*(e[k]-2e[k-1]+e[k-2]) */
    delta = pid->kp * (error - pid->last_error) +
            pid->ki * error +
            pid->kd * (error - 2.0f * pid->last_error + pid->prev_error);

    pid->output += delta;
    pid->output = clampf(pid->output, pid->output_min, pid->output_max);

    pid->prev_error = pid->last_error;
    pid->last_error = error;
    return pid->output;
}