#include "robot_planner.h"

#define ROBOT_PLAN_MAX 8

static robot_plan_t *s_plans[ROBOT_PLAN_MAX];
static robot_plan_t *s_current = NULL;

int robot_planner_init(void)
{
    int i;
    for (i = 0; i < ROBOT_PLAN_MAX; i++) {
        s_plans[i] = NULL;
    }
    s_current = NULL;
    return IOT_OK;
}

static robot_plan_t *planner_find(uint32_t id)
{
    int i;
    for (i = 0; i < ROBOT_PLAN_MAX; i++) {
        if (s_plans[i] != NULL && s_plans[i]->id == id) {
            return s_plans[i];
        }
    }
    return NULL;
}

int robot_planner_add(robot_plan_t *plan)
{
    int i;
    if (plan == NULL || plan->actions == NULL || plan->action_count == 0) {
        return -1;
    }
    for (i = 0; i < ROBOT_PLAN_MAX; i++) {
        if (s_plans[i] == NULL) {
            s_plans[i] = plan;
            break;
        }
    }
    if (i == ROBOT_PLAN_MAX) {
        return -1;
    }
    plan->state = ROBOT_PLAN_PENDING;
    plan->current_action = 0;
    return IOT_OK;
}

int robot_planner_remove(uint32_t id)
{
    int i;
    for (i = 0; i < ROBOT_PLAN_MAX; i++) {
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

int robot_planner_start(uint32_t id)
{
    robot_plan_t *plan = planner_find(id);
    if (plan == NULL) {
        return -1;
    }
    plan->state = ROBOT_PLAN_RUNNING;
    plan->current_action = 0;
    s_current = plan;
    return IOT_OK;
}

int robot_planner_stop(uint32_t id)
{
    robot_plan_t *plan = planner_find(id);
    if (plan == NULL) {
        return -1;
    }
    plan->state = ROBOT_PLAN_PENDING;
    if (s_current == plan) {
        s_current = NULL;
    }
    return IOT_OK;
}

robot_plan_t *robot_planner_get_current(void)
{
    return s_current;
}

int robot_planner_process(void)
{
    robot_plan_t *plan = s_current;

    if (plan == NULL || plan->state != ROBOT_PLAN_RUNNING) {
        return IOT_OK;
    }

    while (plan->current_action < plan->action_count) {
        robot_action_t *a = plan->actions[plan->current_action];
        if (robot_action_get_state(a) == ROBOT_ACTION_IDLE) {
            robot_action_start(a);
        }
        robot_action_update(a);

        switch (robot_action_get_state(a)) {
        case ROBOT_ACTION_SUCCESS:
            plan->current_action++;
            break;
        case ROBOT_ACTION_FAILED:
            plan->state = ROBOT_PLAN_FAILED;
            s_current = NULL;
            return IOT_OK;
        default:
            /* RUNNING：等待下一拍 */
            return IOT_OK;
        }
    }

    plan->state = ROBOT_PLAN_SUCCESS;
    s_current = NULL;
    return IOT_OK;
}