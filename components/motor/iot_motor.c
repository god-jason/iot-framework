#include "iot_motor.h"

int iot_motor_init(iot_motor_t *motor, const iot_motor_ops_t *ops)
{
    if (motor == NULL || ops == NULL) {
        return IOT_ERR_PARAM;
    }
    motor->ops = ops;
    motor->speed = 0;
    motor->max_speed = 1000;
    motor->direction = IOT_MOTOR_STOPPED;
    return IOT_OK;
}

int iot_motor_set_speed(iot_motor_t *motor, int32_t speed)
{
    int32_t s;

    if (motor == NULL || motor->ops == NULL || motor->ops->set_speed == NULL) {
        return IOT_ERR_PARAM;
    }
    /* 限幅到最大速度 */
    s = speed;
    if (s > motor->max_speed) {
        s = motor->max_speed;
    } else if (s < -motor->max_speed) {
        s = -motor->max_speed;
    }
    if (motor->ops->set_speed(motor->ops->ctx, s) != IOT_OK) {
        return IOT_ERR_IO;
    }
    motor->speed = s;
    if (s != 0 && motor->direction == IOT_MOTOR_STOPPED) {
        motor->direction = (s > 0) ? IOT_MOTOR_FORWARD : IOT_MOTOR_REVERSE;
    }
    return IOT_OK;
}

int iot_motor_set_direction(iot_motor_t *motor, iot_motor_direction_t direction)
{
    if (motor == NULL || motor->ops == NULL || motor->ops->set_direction == NULL) {
        return IOT_ERR_PARAM;
    }
    if (motor->ops->set_direction(motor->ops->ctx, direction) != IOT_OK) {
        return IOT_ERR_IO;
    }
    motor->direction = direction;
    return IOT_OK;
}

int iot_motor_stop(iot_motor_t *motor)
{
    if (motor == NULL || motor->ops == NULL) {
        return IOT_ERR_PARAM;
    }
    if (motor->ops->stop != NULL) {
        motor->ops->stop(motor->ops->ctx);
    } else if (motor->ops->set_speed != NULL) {
        motor->ops->set_speed(motor->ops->ctx, 0);
    }
    motor->speed = 0;
    motor->direction = IOT_MOTOR_STOPPED;
    return IOT_OK;
}