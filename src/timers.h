#ifndef IOT_TIMERS_H
#define IOT_TIMERS_H

/*
 * timers.h - 软件定时器（timers.c）
 *
 * 单次/周期定时器，按 deadline 升序挂入内部链表，由 tick 驱动。
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 定时器回调执行上下文：
 *   - PC 模拟端：空闲任务（普通任务上下文）；
 *   - 单片机端：SysTick 中断（ISR 上下文）。
 * 因此回调必须短小，不得调用任何可能阻塞的内核 API
 * （信号量 take、互斥量 lock、队列 send/recv、sleep 等）。
 */
iot_status_t iot_timer_init(iot_timer_t *timer, const char *name,
                              iot_timer_cb_t cb, void *arg,
                              uint32_t period_ms, bool periodic);
iot_status_t iot_timer_start(iot_timer_t *timer, uint32_t period_ms);
iot_status_t iot_timer_stop(iot_timer_t *timer);

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
