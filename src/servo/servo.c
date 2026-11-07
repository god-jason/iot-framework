#include "servo.h"
#include "iot_logger.h"

int servo_init(servo_t *servo, const servo_ops_t *ops,
                   uint16_t min_us, uint16_t max_us,
                   uint16_t min_angle, uint16_t max_angle)
{
    if (servo == NULL || ops == NULL || ops->set_pulse == NULL) {
        return -1;
    }
    servo->ops = ops;
    servo->min_pulse_us = min_us;
    servo->max_pulse_us = max_us;
    servo->min_angle = min_angle;
    servo->max_angle = max_angle;
    servo->angle = min_angle;
    return IOT_OK;
}

int servo_set_angle(servo_t *servo, uint16_t angle)
{
    uint32_t range, us;

    if (servo == NULL) {
        return -1;
    }
    if (angle < servo->min_angle) {
        angle = servo->min_angle;
    }
    if (angle > servo->max_angle) {
        angle = servo->max_angle;
    }
    /* 角度线性映射到脉宽 */
    range = (servo->max_pulse_us - servo->min_pulse_us);
    us = servo->min_pulse_us +
         (uint32_t)(angle - servo->min_angle) * range /
         (servo->max_angle - servo->min_angle);

    servo->angle = angle;
    LOG_I("servo", "angle %u", (unsigned)angle);
    return servo->ops->set_pulse(servo->ops->ctx, (uint16_t)us);
}

int servo_set_pulse(servo_t *servo, uint16_t us)
{
    if (servo == NULL) {
        return -1;
    }
    return servo->ops->set_pulse(servo->ops->ctx, us);
}