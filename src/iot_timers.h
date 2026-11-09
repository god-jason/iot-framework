#ifndef IOT_TIMERS_H
#define IOT_TIMERS_H

/*
 * iot_timers.h - 软件定时器（iot_timers.c）
 *
 * 单次/周期定时器，按 deadline 升序挂入内部链表，由 tick 驱动。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*iot_timer_cb_t)(void *arg);     /* 定时器回调 */

struct iot_timer {
    list_node_t node;               /* 定时器链表节点（排序键 deadline） */
    const char *name;               /* 定时器名 */
    iot_timer_cb_t cb;             /* 到期回调 */
    void *arg;                      /* 回调参数 */
    uint32_t period;                /* 周期（tick） */
    bool periodic;                  /* true=周期，false=单次 */
    bool running;                   /* 是否已启动 */
    uint32_t deadline;              /* 下次到期时刻 */
};

/*
 * 定时器回调执行上下文：
 *   - PC 模拟端：空闲任务（普通任务上下文）；
 *   - 单片机端：SysTick 中断（ISR 上下文）。
 * 因此回调必须短小，不得调用任何可能阻塞的内核 API
 * （sem_take / mutex_lock / queue_send|recv / sleep / event_wait / wait_notify）。
 * ISR 中只能使用 iot_isr.h 提供的 *_from_isr 接口；违规调用会触发断言。
 */
int iot_timer_init(iot_timer_t *timer, const char *name,
                   iot_timer_cb_t cb, void *arg,
                   uint32_t period_ms, bool periodic);
int iot_timer_start(iot_timer_t *timer, uint32_t period_ms);
int iot_timer_stop(iot_timer_t *timer);

/* ---------------- 内核内部接口（iot_tick_increment 调用） ---------------- */

/* 初始化定时器链表 */
void iot_timers_init(void);

/* 定时器链表最早到期时刻；空链表返回 UINT32_MAX */
uint32_t iot_timer_next_deadline(void);

/* 处理所有到期定时器 */
void iot_timer_tick(uint32_t now);

#ifdef __cplusplus
}
#endif

#endif /* IOT_TIMERS_H */
