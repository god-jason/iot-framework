#include "planner.h"

#define PLANNER_PLAN_MAX 8

static planner_plan_t *s_plans[PLANNER_PLAN_MAX];
static planner_plan_t *s_current = NULL;

int planner_init(void)
{
    int i;
    for (i = 0; i < PLANNER_PLAN_MAX; i++) {
        s_plans[i] = NULL;
    }
    s_current = NULL;
    return IOT_OK;
}

static planner_plan_t *planner_find(uint32_t id)
{
    int i;
    for (i = 0; i < PLANNER_PLAN_MAX; i++) {
        if (s_plans[i] != NULL && s_plans[i]->id == id) {
            return s_plans[i];
        }
    }
    return NULL;
}

int planner_add(planner_plan_t *plan)
{
    int i;
    if (plan == NULL || plan->actions == NULL || plan->action_count == 0) {
        return -1;
    }
    for (i = 0; i < PLANNER_PLAN_MAX; i++) {
        if (s_plans[i] == NULL) {
            s_plans[i] = plan;
            break;
        }
    }
    if (i == PLANNER_PLAN_MAX) {
        return -1;
    }
    plan->state = PLANNER_PLAN_PENDING;
    plan->current_action = 0;
    return IOT_OK;
}

int planner_remove(uint32_t id)
{
    int i;
    for (i = 0; i < PLANNER_PLAN_MAX; i++) {
        if (s_plans[i] != NULL && s_plans[i]->id == id) {
            if (s_current == s_plans[i]) {
                s_current = NULL;
            }
            s_plans[i] = NULL;
            return IOT_OK;
        }
    }
    return -1;
}

int planner_start(uint32_t id)
{
    planner_plan_t *plan = planner_find(id);
    if (plan == NULL) {
        return -1;
    }
    plan->state = PLANNER_PLAN_RUNNING;
    plan->current_action = 0;
    s_current = plan;
    return IOT_OK;
}

int planner_stop(uint32_t id)
{
    planner_plan_t *plan = planner_find(id);
    if (plan == NULL) {
        return -1;
    }
    plan->state = PLANNER_PLAN_PENDING;
    if (s_current == plan) {
        s_current = NULL;
    }
    return IOT_OK;
}

planner_plan_t *planner_get_current(void)
{
    return s_current;
}

int planner_process(void)
{
    planner_plan_t *plan = s_current;

    if (plan == NULL || plan->state != PLANNER_PLAN_RUNNING) {
        return IOT_OK;
    }

    while (plan->current_action < plan->action_count) {
        action_t *a = plan->actions[plan->current_action];
        if (action_get_state(a) == ACTION_IDLE) {
            action_start(a);
        }
        action_update(a);

        switch (action_get_state(a)) {
        case ACTION_SUCCESS:
            plan->current_action++;
            break;
        case ACTION_FAILED:
            plan->state = PLANNER_PLAN_FAILED;
            s_current = NULL;
            return IOT_OK;
        default:
            /* RUNNING：等待下一拍 */
            return IOT_OK;
        }
    }

    plan->state = PLANNER_PLAN_SUCCESS;
    s_current = NULL;
    return IOT_OK;
}