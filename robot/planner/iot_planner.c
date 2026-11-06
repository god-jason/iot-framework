#include "iot_planner.h"

#define IOT_PLAN_MAX 8

static iot_plan_t *s_plans[IOT_PLAN_MAX];
static iot_plan_t *s_current = NULL;

int iot_planner_init(void)
{
    int i;
    for (i = 0; i < IOT_PLAN_MAX; i++) {
        s_plans[i] = NULL;
    }
    s_current = NULL;
    return IOT_OK;
}

static iot_plan_t *planner_find(uint32_t id)
{
    int i;
    for (i = 0; i < IOT_PLAN_MAX; i++) {
        if (s_plans[i] != NULL && s_plans[i]->id == id) {
            return s_plans[i];
        }
    }
    return NULL;
}

int iot_planner_add(iot_plan_t *plan)
{
    int i;
    if (plan == NULL || plan->actions == NULL || plan->action_count == 0) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_PLAN_MAX; i++) {
        if (s_plans[i] == NULL) {
            s_plans[i] = plan;
            break;
        }
    }
    if (i == IOT_PLAN_MAX) {
        return IOT_ERR_OVERFLOW;
    }
    plan->state = IOT_PLAN_PENDING;
    plan->current_action = 0;
    return IOT_OK;
}

int iot_planner_remove(uint32_t id)
{
    int i;
    for (i = 0; i < IOT_PLAN_MAX; i++) {
        if (s_plans[i] != NULL && s_plans[i]->id == id) {
            if (s_current == s_plans[i]) {
                s_current = NULL;
            }
            s_plans[i] = NULL;
            return IOT_OK;
        }
    }
    return IOT_ERR_NOT_FOUND;
}

int iot_planner_start(uint32_t id)
{
    iot_plan_t *plan = planner_find(id);
    if (plan == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    plan->state = IOT_PLAN_RUNNING;
    plan->current_action = 0;
    s_current = plan;
    return IOT_OK;
}

int iot_planner_stop(uint32_t id)
{
    iot_plan_t *plan = planner_find(id);
    if (plan == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    plan->state = IOT_PLAN_PENDING;
    if (s_current == plan) {
        s_current = NULL;
    }
    return IOT_OK;
}

iot_plan_t *iot_planner_get_current(void)
{
    return s_current;
}

int iot_planner_process(void)
{
    iot_plan_t *plan = s_current;

    if (plan == NULL || plan->state != IOT_PLAN_RUNNING) {
        return IOT_OK;
    }

    while (plan->current_action < plan->action_count) {
        iot_action_t *a = plan->actions[plan->current_action];
        if (iot_action_get_state(a) == IOT_ACTION_IDLE) {
            iot_action_start(a);
        }
        iot_action_update(a);

        switch (iot_action_get_state(a)) {
        case IOT_ACTION_SUCCESS:
            plan->current_action++;
            break;
        case IOT_ACTION_FAILED:
            plan->state = IOT_PLAN_FAILED;
            s_current = NULL;
            return IOT_OK;
        default:
            /* RUNNING：等待下一拍 */
            return IOT_OK;
        }
    }

    plan->state = IOT_PLAN_SUCCESS;
    s_current = NULL;
    return IOT_OK;
}