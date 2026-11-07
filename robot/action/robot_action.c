#include "robot_action.h"

int robot_action_init(robot_action_t *action)
{
    if (action == NULL) {
        return -1;
    }
    action->state = ROBOT_ACTION_IDLE;
    return IOT_OK;
}

int robot_action_start(robot_action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    ret = (action->start != NULL) ? action->start(action) : IOT_OK;
    if (ret == IOT_OK) {
        action->state = ROBOT_ACTION_RUNNING;
    } else {
        action->state = ROBOT_ACTION_FAILED;
    }
    return ret;
}

int robot_action_update(robot_action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    if (action->state != ROBOT_ACTION_RUNNING) {
        return -1;
    }
    ret = (action->update != NULL) ? action->update(action) : 1;
    if (ret == 0) {
        action->state = ROBOT_ACTION_RUNNING;
    } else if (ret > 0) {
        action->state = ROBOT_ACTION_SUCCESS;
    } else {
        action->state = ROBOT_ACTION_FAILED;
    }
    return IOT_OK;
}

int robot_action_cancel(robot_action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    ret = (action->cancel != NULL) ? action->cancel(action) : IOT_OK;
    action->state = ROBOT_ACTION_CANCELLED;
    return ret;
}

int robot_action_reset(robot_action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    ret = (action->reset != NULL) ? action->reset(action) : IOT_OK;
    action->state = ROBOT_ACTION_IDLE;
    return ret;
}

robot_action_state_t robot_action_get_state(const robot_action_t *action)
{
    return (action == NULL) ? ROBOT_ACTION_IDLE : action->state;
}