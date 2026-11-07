#include "stepper.h"
#include "iot_logger.h"

int stepper_init(stepper_t *st, const stepper_ops_t *ops)
{
    if (st == NULL || ops == NULL || ops->step == NULL) {
        return -1;
    }
    st->ops = ops;
    st->position = 0;
    st->direction = 0;
    return IOT_OK;
}

int stepper_set_direction(stepper_t *st, uint8_t dir)
{
    if (st == NULL) {
        return -1;
    }
    st->direction = dir & 1u;
    if (st->ops->set_dir != NULL) {
        if (st->ops->set_dir(st->ops->ctx, st->direction) != IOT_OK) {
            return -1;
        }
    }
    return IOT_OK;
}

int stepper_move(stepper_t *st, int32_t steps)
{
    int32_t i;
    uint8_t dir;

    if (st == NULL) {
        return -1;
    }
    dir = (steps >= 0) ? 0 : 1;
    if (stepper_set_direction(st, dir) != IOT_OK) {
        return -1;
    }
    if (steps < 0) {
        steps = -steps;
    }
    for (i = 0; i < steps; i++) {
        if (st->ops->step(st->ops->ctx) != IOT_OK) {
            LOG_E("stepper", "step fail");   /* step pulse failed */
            return -1;
        }
        st->position += (dir == 0) ? 1 : -1;
    }
    return IOT_OK;
}

int stepper_move_to(stepper_t *st, int32_t target)
{
    if (st == NULL) {
        return -1;
    }
    return stepper_move(st, target - st->position);
}

int stepper_get_position(stepper_t *st, int32_t *position)
{
    if (st == NULL || position == NULL) {
        return -1;
    }
    *position = st->position;
    return IOT_OK;
}