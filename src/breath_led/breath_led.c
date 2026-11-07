#include "breath_led.h"
#include "iot_logger.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

int breath_led_init(breath_led_t *led, const breath_led_ops_t *ops)
{
    if (led == NULL || ops == NULL || ops->set_duty == NULL) {
        return -1;
    }
    led->ops = ops;
    led->duty = 0;
    led->period_ms = 2000;
    led->start_tick = 0;
    led->started = false;
    led->active = false;
    return IOT_OK;
}

int breath_led_start(breath_led_t *led, uint16_t period_ms)
{
    if (led == NULL || period_ms == 0) {
        return -1;
    }
    led->period_ms = period_ms;
    led->active = true;
    led->started = false;
    led->start_tick = 0;
    LOG_I("breath", "start");
    return IOT_OK;
}

int breath_led_update(breath_led_t *led, uint32_t tick_ms)
{
    float phase, duty;

    if (led == NULL || !led->active) {
        return -1;
    }
    if (!led->started) {
        led->start_tick = tick_ms;
        led->started = true;
    }
    /* 正弦呼吸：sin(0..π) 从 0 升到 1 再降回 0 */
    phase = (float)(tick_ms - led->start_tick) / (float)led->period_ms;
    duty = sinf(phase * (float)M_PI) * 1000.0f;
    led->duty = (uint16_t)duty;
    return led->ops->set_duty(led->ops->ctx, led->duty);
}

int breath_led_stop(breath_led_t *led)
{
    if (led == NULL) {
        return -1;
    }
    led->active = false;
    led->duty = 0;
    return led->ops->set_duty(led->ops->ctx, 0);
}