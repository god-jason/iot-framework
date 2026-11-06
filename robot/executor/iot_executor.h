#ifndef IOT_EXECUTOR_H
#define IOT_EXECUTOR_H

/*
 * 执行器：驱动单个 plan（顺序执行其 Action），带计数与启停控制。
 */

#include "iot_planner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    iot_plan_t *current_plan;

    bool running;
    bool paused;

    uint32_t completed_count;
    uint32_t failed_count;
} iot_executor_t;

int iot_executor_init(iot_executor_t *executor);

int iot_executor_start(iot_executor_t *executor, iot_plan_t *plan);
int iot_executor_process(iot_executor_t *executor);

int iot_executor_pause(iot_executor_t *executor);
int iot_executor_resume(iot_executor_t *executor);
int iot_executor_stop(iot_executor_t *executor);

iot_action_t *iot_executor_get_current_action(iot_executor_t *executor);

#ifdef __cplusplus
}
#endif

#endif /* IOT_EXECUTOR_H */