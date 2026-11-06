#include "iot_action.h"

int iot_action_init(iot_action_t *action)
{
    if (action == NULL) {
        return IOT_ERR_PARAM;
    }
    action->state = IOT_ACTION_IDLE;
    return IOT_OK;
}

int iot_action_start(iot_action_t *action)
{
    int ret;
    if (action == NULL) {
        return IOT_ERR_PARAM;
    }
    ret = (action->start != NULL) ? action->start(action) : IOT_OK;
    if (ret == IOT_OK) {
        action->state = IOT_ACTION_RUNNING;
    } else {
        action->state = IOT_ACTION_FAILED;
    }
    return ret;
}

int iot_action_update(iot_action_t *action)
{
    int ret;
    if (action == NULL) {
        return IOT_ERR_PARAM;
    }
    if (action->state != IOT_ACTION_RUNNING) {
        return IOT_ERR_STATE;
    }
    ret = (action->update != NULL) ? action->update(action) : 1;
    if (ret == 0) {
        action->state = IOT_ACTION_RUNNING;
    } else if (ret > 0) {
        action->state = IOT_ACTION_SUCCESS;
    } else {
        action->state = IOT_ACTION_FAILED;
    }
    return IOT_OK;
}

int iot_action_cancel(iot_action_t *action)
{
    int ret;
    if (action == NULL) {
        return IOT_ERR_PARAM;
    }
    ret = (action->cancel != NULL) ? action->cancel(action) : IOT_OK;
    action->state = IOT_ACTION_CANCELLED;
    return ret;
}

int iot_action_reset(iot_action_t *action)
{
    int ret;
    if (action == NULL) {
        return IOT_ERR_PARAM;
    }
    ret = (action->reset != NULL) ? action->reset(action) : IOT_OK;
    action->state = IOT_ACTION_IDLE;
    return ret;
}

iot_action_state_t iot_action_get_state(const iot_action_t *action)
{
    return (action == NULL) ? IOT_ACTION_IDLE : action->state;
}