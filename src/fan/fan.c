#include "fan.h"
#include "iot_logger.h"

int fan_init(fan_t *fan, const fan_ops_t *ops)
{
    if (fan == NULL || ops == NULL || ops->set_duty == NULL) {
        return -1;
    }
    fan->ops = ops;
    fan->duty = 0;
    return fan_off(fan);
}

int fan_set_speed(fan_t *fan, uint16_t duty)
{
    if (fan == NULL) {
        return -1;
    }
    if (duty > 1000) {
        duty = 1000;
    }
    fan->duty = duty;
    return fan->ops->set_duty(fan->ops->ctx, duty);
}

int fan_on(fan_t *fan)
{
    if (fan == NULL) {
        return -1;
    }
    LOG_I("fan", "on");
    return fan_set_speed(fan, 1000);
}

int fan_off(fan_t *fan)
{
    if (fan == NULL) {
        return -1;
    }
    return fan_set_speed(fan, 0);
}