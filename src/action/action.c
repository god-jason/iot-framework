#include "action.h"

int action_init(action_t *action)
{
    if (action == NULL) {
        return -1;
    }
    action->state = ACTION_IDLE;
    return IOT_OK;
}

int action_start(action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    ret = (action->start != NULL) ? action->start(action) : IOT_OK;
    if (ret == IOT_OK) {
        action->state = ACTION_RUNNING;
    } else {
        action->state = ACTION_FAILED;
    }
    return ret;
}

int action_update(action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    if (action->state != ACTION_RUNNING) {
        return -1;
    }
    ret = (action->update != NULL) ? action->update(action) : 1;
    if (ret == 0) {
        action->state = ACTION_RUNNING;
    } else if (ret > 0) {
        action->state = ACTION_SUCCESS;
    } else {
        action->state = ACTION_FAILED;
    }
    return IOT_OK;
}

int action_cancel(action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    ret = (action->cancel != NULL) ? action->cancel(action) : IOT_OK;
    action->state = ACTION_CANCELLED;
    return ret;
}

int action_reset(action_t *action)
{
    int ret;
    if (action == NULL) {
        return -1;
    }
    ret = (action->reset != NULL) ? action->reset(action) : IOT_OK;
    action->state = ACTION_IDLE;
    return ret;
}

action_state_t action_get_state(const action_t *action)
{
    return (action == NULL) ? ACTION_IDLE : action->state;
}