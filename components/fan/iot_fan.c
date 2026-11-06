#include "iot_fan.h"
#include "iot_logger.h"

int iot_fan_init(iot_fan_t *fan, const iot_fan_ops_t *ops)
{
    if (fan == NULL || ops == NULL || ops->set_duty == NULL) {
        return IOT_ERR_PARAM;
    }
    fan->ops = ops;
    fan->duty = 0;
    return iot_fan_off(fan);
}

int iot_fan_set_speed(iot_fan_t *fan, uint16_t duty)
{
    if (fan == NULL) {
        return IOT_ERR_PARAM;
    }
    if (duty > 1000) {
        duty = 1000;
    }
    fan->duty = duty;
    return fan->ops->set_duty(fan->ops->ctx, duty);
}

int iot_fan_on(iot_fan_t *fan)
{
    if (fan == NULL) {
        return IOT_ERR_PARAM;
    }
    LOG_I("fan", "on");
    return iot_fan_set_speed(fan, 1000);
}

int iot_fan_off(iot_fan_t *fan)
{
    if (fan == NULL) {
        return IOT_ERR_PARAM;
    }
    return iot_fan_set_speed(fan, 0);
}