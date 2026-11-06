#include "iot_executor.h"

int iot_executor_init(iot_executor_t *executor)
{
    if (executor == NULL) {
        return IOT_ERR_PARAM;
    }
    executor->current_plan = NULL;
    executor->running = false;
    executor->paused = false;
    executor->completed_count = 0;
    executor->failed_count = 0;
    return IOT_OK;
}

int iot_executor_start(iot_executor_t *executor, iot_plan_t *plan)
{
    if (executor == NULL || plan == NULL) {
        return IOT_ERR_PARAM;
    }
    executor->current_plan = plan;
    executor->running = true;
    executor->paused = false;
    plan->current_action = 0;
    plan->state = IOT_PLAN_RUNNING;
    return IOT_OK;
}

int iot_executor_process(iot_executor_t *executor)
{
    iot_plan_t *plan;

    if (executor == NULL || !executor->running || executor->paused) {
        return IOT_OK;
    }
    plan = executor->current_plan;
    if (plan == NULL) {
        return IOT_ERR_STATE;
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
            executor->failed_count++;
            executor->running = false;
            return IOT_OK;
        default:
            return IOT_OK;
        }
    }

    plan->state = IOT_PLAN_SUCCESS;
    executor->completed_count++;
    executor->running = false;
    return IOT_OK;
}

int iot_executor_pause(iot_executor_t *executor)
{
    if (executor == NULL) {
        return IOT_ERR_PARAM;
    }
    executor->paused = true;
    return IOT_OK;
}

int iot_executor_resume(iot_executor_t *executor)
{
    if (executor == NULL) {
        return IOT_ERR_PARAM;
    }
    executor->paused = false;
    return IOT_OK;
}

int iot_executor_stop(iot_executor_t *executor)
{
    if (executor == NULL) {
        return IOT_ERR_PARAM;
    }
    executor->running = false;
    executor->paused = false;
    if (executor->current_plan != NULL) {
        executor->current_plan->state = IOT_PLAN_PENDING;
    }
    return IOT_OK;
}

iot_action_t *iot_executor_get_current_action(iot_executor_t *executor)
{
    iot_plan_t *plan;
    if (executor == NULL) {
        return NULL;
    }
    plan = executor->current_plan;
    if (plan == NULL || plan->current_action >= plan->action_count) {
        return NULL;
    }
    return plan->actions[plan->current_action];
}