#ifndef IOT_PLANNER_H
#define IOT_PLANNER_H

/*
 * 任务规划器：按顺序执行一组 Action。
 */

#include "iot_types.h"
#include "iot_action.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_PLAN_PENDING,
    IOT_PLAN_RUNNING,
    IOT_PLAN_SUCCESS,
    IOT_PLAN_FAILED
} iot_plan_state_t;

typedef struct {
    uint32_t id;
    const char *name;

    uint8_t priority;
    iot_plan_state_t state;

    iot_action_t **actions;
    uint16_t action_count;
    uint16_t current_action;

    void *ctx;
} iot_plan_t;

int iot_planner_init(void);

int iot_planner_add(iot_plan_t *plan);
int iot_planner_remove(uint32_t id);

int iot_planner_start(uint32_t id);
int iot_planner_stop(uint32_t id);

int iot_planner_process(void);

iot_plan_t *iot_planner_get_current(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PLANNER_H */