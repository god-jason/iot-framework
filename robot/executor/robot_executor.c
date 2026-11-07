#include "robot_executor.h"

int robot_executor_init(robot_executor_t *executor)
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

int robot_executor_start(robot_executor_t *executor, robot_plan_t *plan)
{
    if (executor == NULL || plan == NULL) {
        return -1;
    }
    executor->current_plan = plan;
    executor->running = true;
    executor->paused = false;
    plan->current_action = 0;
    plan->state = ROBOT_PLAN_RUNNING;
    return IOT_OK;
}

int robot_executor_process(robot_executor_t *executor)
{
    robot_plan_t *plan;

    if (executor == NULL || !executor->running || executor->paused) {
        return IOT_OK;
    }
    plan = executor->current_plan;
    if (plan == NULL) {
        return -1;
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
            executor->failed_count++;
            executor->running = false;
            return IOT_OK;
        default:
            return IOT_OK;
        }
    }

    plan->state = ROBOT_PLAN_SUCCESS;
    executor->completed_count++;
    executor->running = false;
    return IOT_OK;
}

int robot_executor_pause(robot_executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->paused = true;
    return IOT_OK;
}

int robot_executor_resume(robot_executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->paused = false;
    return IOT_OK;
}

int robot_executor_stop(robot_executor_t *executor)
{
    if (executor == NULL) {
        return -1;
    }
    executor->running = false;
    executor->paused = false;
    if (executor->current_plan != NULL) {
        executor->current_plan->state = ROBOT_PLAN_PENDING;
    }
    return IOT_OK;
}

robot_action_t *robot_executor_get_current_action(robot_executor_t *executor)
{
    robot_plan_t *plan;
    if (executor == NULL) {
        return NULL;
    }
    plan = executor->current_plan;
    if (plan == NULL || plan->current_action >= plan->action_count) {
        return NULL;
    }
    return plan->actions[plan->current_action];
}