#include "iot_timer.h"

#define IOT_TIMER_MAX 16

static iot_timer_t *s_timers[IOT_TIMER_MAX];
static iot_tick_t s_tick = 0;

int iot_timer_init(void)
{
    int i;
    for (i = 0; i < IOT_TIMER_MAX; i++) {
        s_timers[i] = NULL;
    }
    s_tick = 0;
    return IOT_OK;
}

int iot_timer_create(iot_timer_t *timer, uint32_t period, bool repeat,
                     iot_timer_fn callback, void *user)
{
    int i;
    if (timer == NULL) {
        return -1;
    }
    for (i = 0; i < IOT_TIMER_MAX; i++) {
        if (s_timers[i] == timer) {
            return -1;
        }
    }
    for (i = 0; i < IOT_TIMER_MAX; i++) {
        if (s_timers[i] == NULL) {
            s_timers[i] = timer;
            break;
        }
    }
    if (i == IOT_TIMER_MAX) {
        return -1;
    }

    timer->period = period;
    timer->timeout = 0;
    timer->repeat = repeat;
    timer->active = false;
    timer->callback = callback;
    timer->user = user;
    timer->next = NULL;
    return IOT_OK;
}

int iot_timer_start(iot_timer_t *timer)
{
    if (timer == NULL) {
        return -1;
    }
    if (timer->active) {
        return -1;
    }
    timer->timeout = s_tick + timer->period;
    timer->active = true;
    return IOT_OK;
}

int iot_timer_stop(iot_timer_t *timer)
{
    if (timer == NULL) {
        return -1;
    }
    timer->active = false;
    return IOT_OK;
}

int iot_timer_reset(iot_timer_t *timer)
{
    if (timer == NULL) {
        return -1;
    }
    timer->timeout = s_tick + timer->period;
    timer->active = true;
    return IOT_OK;
}

int iot_timer_delete(iot_timer_t *timer)
{
    int i;
    if (timer == NULL) {
        return -1;
    }
    timer->active = false;
    timer->callback = NULL;
    for (i = 0; i < IOT_TIMER_MAX; i++) {
        if (s_timers[i] == timer) {
            s_timers[i] = NULL;
            return IOT_OK;
        }
    }
    return -1;
}

bool iot_timer_is_active(const iot_timer_t *timer)
{
    return (timer != NULL) && timer->active;
}

int iot_timer_process(void)
{
    int i;

    s_tick++;

    for (i = 0; i < IOT_TIMER_MAX; i++) {
        iot_timer_t *t = s_timers[i];
        if (t == NULL || !t->active) {
            continue;
        }
        if (s_tick < t->timeout) {
            continue;
        }
        if (t->callback != NULL) {
            t->callback(t, t->user);
        }
        if (t->repeat) {
            t->timeout = s_tick + t->period;
        } else {
            t->active = false;
        }
    }
    return IOT_OK;
}