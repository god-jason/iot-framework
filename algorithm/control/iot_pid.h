#ifndef IOT_PID_H
#define IOT_PID_H

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
} iot_pid_t;

int iot_pid_init(iot_pid_t *pid, float kp, float ki, float kd);
float iot_pid_update(iot_pid_t *pid, float target, float measurement, float dt);
void iot_pid_reset(iot_pid_t *pid);
void iot_pid_set_params(iot_pid_t *pid, float kp, float ki, float kd);
void iot_pid_set_limits(iot_pid_t *pid, float min, float max);

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
} iot_pid_incremental_t;

int iot_pid_incremental_init(iot_pid_incremental_t *pid, float kp, float ki, float kd);
float iot_pid_incremental_update(iot_pid_incremental_t *pid, float target, float measurement);
void iot_pid_incremental_reset(iot_pid_incremental_t *pid);
void iot_pid_incremental_set_limits(iot_pid_incremental_t *pid, float min, float max);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PID_H */