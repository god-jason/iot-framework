#ifndef ROBOT_EXECUTOR_H
#define ROBOT_EXECUTOR_H

/*
 * 执行器：驱动单个 plan（顺序执行其 Action），带计数与启停控制。
 */

#include "robot_planner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    robot_plan_t *current_plan;

    bool running;
    bool paused;

    uint32_t completed_count;
    uint32_t failed_count;
} robot_executor_t;

int robot_executor_init(robot_executor_t *executor);

int robot_executor_start(robot_executor_t *executor, robot_plan_t *plan);
int robot_executor_process(robot_executor_t *executor);

int robot_executor_pause(robot_executor_t *executor);
int robot_executor_resume(robot_executor_t *executor);
int robot_executor_stop(robot_executor_t *executor);

robot_action_t *robot_executor_get_current_action(robot_executor_t *executor);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_EXECUTOR_H */