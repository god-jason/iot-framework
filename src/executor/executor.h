#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * 执行器：驱动单个 plan（顺序执行其 Action），带计数与启停控制。
 */

#include "planner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    planner_plan_t *current_plan;

    bool running;
    bool paused;

    uint32_t completed_count;
    uint32_t failed_count;
} executor_t;

int executor_init(executor_t *executor);

int executor_start(executor_t *executor, planner_plan_t *plan);
int executor_process(executor_t *executor);

int executor_pause(executor_t *executor);
int executor_resume(executor_t *executor);
int executor_stop(executor_t *executor);

action_t *executor_get_current_action(executor_t *executor);

#ifdef __cplusplus
}
#endif

#endif /* EXECUTOR_H */