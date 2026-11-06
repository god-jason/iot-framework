#include "iot_stepper.h"
#include "iot_logger.h"

int iot_stepper_init(iot_stepper_t *st, const iot_stepper_ops_t *ops)
{
    if (st == NULL || ops == NULL || ops->step == NULL) {
        return IOT_ERR_PARAM;
    }
    st->ops = ops;
    st->position = 0;
    st->direction = 0;
    return IOT_OK;
}

int iot_stepper_set_direction(iot_stepper_t *st, uint8_t dir)
{
    if (st == NULL) {
        return IOT_ERR_PARAM;
    }
    st->direction = dir & 1u;
    if (st->ops->set_dir != NULL) {
        if (st->ops->set_dir(st->ops->ctx, st->direction) != IOT_OK) {
            return IOT_ERR_IO;
        }
    }
    return IOT_OK;
}

int iot_stepper_move(iot_stepper_t *st, int32_t steps)
{
    int32_t i;
    uint8_t dir;

    if (st == NULL) {
        return IOT_ERR_PARAM;
    }
    dir = (steps >= 0) ? 0 : 1;
    if (iot_stepper_set_direction(st, dir) != IOT_OK) {
        return IOT_ERR_IO;
    }
    if (steps < 0) {
        steps = -steps;
    }
    for (i = 0; i < steps; i++) {
        if (st->ops->step(st->ops->ctx) != IOT_OK) {
            LOG_E("stepper", "step fail");   /* step pulse failed */
            return IOT_ERR_IO;
        }
        st->position += (dir == 0) ? 1 : -1;
    }
    return IOT_OK;
}

int iot_stepper_move_to(iot_stepper_t *st, int32_t target)
{
    if (st == NULL) {
        return IOT_ERR_PARAM;
    }
    return iot_stepper_move(st, target - st->position);
}

int iot_stepper_get_position(iot_stepper_t *st, int32_t *position)
{
    if (st == NULL || position == NULL) {
        return IOT_ERR_PARAM;
    }
    *position = st->position;
    return IOT_OK;
}