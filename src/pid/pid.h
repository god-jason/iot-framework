#ifndef PID_H
#define PID_H

/*
 * PID 控制（位置式 + 增量式）。算法库独立于 Core 与 HAL。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 位置式 PID ---- */
typedef struct {
    float kp;
    float ki;
    float kd;

    float target;
    float measurement;

    float output;
    float output_min;
    float output_max;

    float integral;
    float last_error;

    bool initialized;
} pid_t;

int pid_init(pid_t *pid, float kp, float ki, float kd);
float pid_update(pid_t *pid, float target, float measurement, float dt);
void pid_reset(pid_t *pid);
void pid_set_params(pid_t *pid, float kp, float ki, float kd);
void pid_set_limits(pid_t *pid, float min, float max);

/* ---- 增量式 PID（定周期，dt 隐含） ---- */
typedef struct {
    float kp;
    float ki;
    float kd;

    float last_error;
    float prev_error;

    float output;
    float output_min;
    float output_max;

    bool initialized;
} pid_incremental_t;

int pid_incremental_init(pid_incremental_t *pid, float kp, float ki, float kd);
float pid_incremental_update(pid_incremental_t *pid, float target, float measurement);
void pid_incremental_reset(pid_incremental_t *pid);
void pid_incremental_set_limits(pid_incremental_t *pid, float min, float max);

#ifdef __cplusplus
}
#endif

#endif /* PID_H */