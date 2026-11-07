#include "motor.h"

int motor_init(motor_t *motor, const motor_ops_t *ops)
{
    if (motor == NULL || ops == NULL) {
        return -1;
    }
    motor->ops = ops;
    motor->speed = 0;
    motor->max_speed = 1000;
    motor->direction = MOTOR_STOPPED;
    return IOT_OK;
}

int motor_set_speed(motor_t *motor, int32_t speed)
{
    int32_t s;

    if (motor == NULL || motor->ops == NULL || motor->ops->set_speed == NULL) {
        return -1;
    }
    /* 限幅到最大速度 */
    s = speed;
    if (s > motor->max_speed) {
        s = motor->max_speed;
    } else if (s < -motor->max_speed) {
        s = -motor->max_speed;
    }
    if (motor->ops->set_speed(motor->ops->ctx, s) != IOT_OK) {
        return -1;
    }
    motor->speed = s;
    if (s != 0 && motor->direction == MOTOR_STOPPED) {
        motor->direction = (s > 0) ? MOTOR_FORWARD : MOTOR_REVERSE;
    }
    return IOT_OK;
}

int motor_set_direction(motor_t *motor, motor_direction_t direction)
{
    if (motor == NULL || motor->ops == NULL || motor->ops->set_direction == NULL) {
        return -1;
    }
    if (motor->ops->set_direction(motor->ops->ctx, direction) != IOT_OK) {
        return -1;
    }
    motor->direction = direction;
    return IOT_OK;
}

int motor_stop(motor_t *motor)
{
    if (motor == NULL || motor->ops == NULL) {
        return -1;
    }
    if (motor->ops->stop != NULL) {
        motor->ops->stop(motor->ops->ctx);
    } else if (motor->ops->set_speed != NULL) {
        motor->ops->set_speed(motor->ops->ctx, 0);
    }
    motor->speed = 0;
    motor->direction = MOTOR_STOPPED;
    return IOT_OK;
}