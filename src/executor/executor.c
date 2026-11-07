#include "executor.h"

int executor_init(executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->current_plan = NULL;
    executor->running = false;
    executor->paused = false;
    executor->completed_count = 0;
    executor->failed_count = 0;
    return IOT_OK;
}

int executor_start(executor_t *executor, planner_plan_t *plan)
{
    if (executor == NULL || plan == NULL) {
        return -1;
    }
    executor->current_plan = plan;
    executor->running = true;
    executor->paused = false;
    plan->current_action = 0;
    plan->state = PLANNER_PLAN_RUNNING;
    return IOT_OK;
}

int executor_process(executor_t *executor)
{
    planner_plan_t *plan;

    if (executor == NULL || !executor->running || executor->paused) {
        return IOT_OK;
    }
    plan = executor->current_plan;
    if (plan == NULL) {
        return -1;
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
            executor->failed_count++;
            executor->running = false;
            return IOT_OK;
        default:
            return IOT_OK;
        }
    }

    plan->state = PLANNER_PLAN_SUCCESS;
    executor->completed_count++;
    executor->running = false;
    return IOT_OK;
}

int executor_pause(executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->paused = true;
    return IOT_OK;
}

int executor_resume(executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->paused = false;
    return IOT_OK;
}

int executor_stop(executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->running = false;
    executor->paused = false;
    if (executor->current_plan != NULL) {
        executor->current_plan->state = PLANNER_PLAN_PENDING;
    }
    return IOT_OK;
}

action_t *executor_get_current_action(executor_t *executor)
{
    planner_plan_t *plan;
    if (executor == NULL) {
        return NULL;
    }
    plan = executor->current_plan;
    if (plan == NULL || plan->current_action >= plan->action_count) {
        return NULL;
    }
    return plan->actions[plan->current_action];
}