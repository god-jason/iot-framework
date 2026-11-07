#ifndef PLANNER_H
#define PLANNER_H

/*
 * 任务规划器：按顺序执行一组 Action。
 */

#include "iot_types.h"
#include "action.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLANNER_PLAN_PENDING,
    PLANNER_PLAN_RUNNING,
    PLANNER_PLAN_SUCCESS,
    PLANNER_PLAN_FAILED
} planner_plan_state_t;

typedef struct {
    uint32_t id;
    const char *name;

    uint8_t priority;
    planner_plan_state_t state;

    action_t **actions;
    uint16_t action_count;
    uint16_t current_action;

    void *ctx;
} planner_plan_t;

int planner_init(void);

int planner_add(planner_plan_t *plan);
int planner_remove(uint32_t id);

int planner_start(uint32_t id);
int planner_stop(uint32_t id);

int planner_process(void);

planner_plan_t *planner_get_current(void);

#ifdef __cplusplus
}
#endif

#endif /* PLANNER_H */