#ifndef IOT_TIMER_H
#define IOT_TIMER_H

/*
 * 软件定时器。定时源为内部 tick，每个 iot_timer_process() 调用前进 1 tick，
 * 因此需要在固定周期的主循环（典型 1ms SysTick）中调用。
 * period / timeout 单位均为 tick。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iot_timer iot_timer_t;

typedef void (*iot_timer_fn)(iot_timer_t *timer, void *user);

struct iot_timer {
    uint32_t period;    /* 周期（tick） */
    uint32_t timeout;   /* 到期 tick（内部使用） */

    bool repeat;        /* true=周期，false=单次 */
    bool active;

    iot_timer_fn callback;
    void *user;

    void *next;         /* 保留字段，当前未使用 */
};

int iot_timer_init(void);

int iot_timer_create(iot_timer_t *timer, uint32_t period, bool repeat,
                     iot_timer_fn callback, void *user);

int iot_timer_start(iot_timer_t *timer);
int iot_timer_stop(iot_timer_t *timer);
int iot_timer_reset(iot_timer_t *timer);
int iot_timer_delete(iot_timer_t *timer);

int iot_timer_process(void);

bool iot_timer_is_active(const iot_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif /* IOT_TIMER_H */