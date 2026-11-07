#ifndef ROBOT_PLANNER_H
#define ROBOT_PLANNER_H

/*
 * 任务规划器：按顺序执行一组 Action。
 */

#include "iot_types.h"
#include "robot_action.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROBOT_PLAN_PENDING,
    ROBOT_PLAN_RUNNING,
    ROBOT_PLAN_SUCCESS,
    ROBOT_PLAN_FAILED
} robot_plan_state_t;

typedef struct {
    uint32_t id;
    const char *name;

    uint8_t priority;
    robot_plan_state_t state;

    robot_action_t **actions;
    uint16_t action_count;
    uint16_t current_action;

    void *ctx;
} robot_plan_t;

int robot_planner_init(void);

int robot_planner_add(robot_plan_t *plan);
int robot_planner_remove(uint32_t id);

int robot_planner_start(uint32_t id);
int robot_planner_stop(uint32_t id);

int robot_planner_process(void);

robot_plan_t *robot_planner_get_current(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_PLANNER_H */