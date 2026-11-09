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
    list_node_t pending_node;       /* 到期待执行链表节点（任务模式） */
    const char *name;               /* 定时器名 */
    iot_timer_cb_t cb;             /* 到期回调 */
    void *arg;                      /* 回调参数 */
    uint32_t period;                /* 周期（tick） */
    bool periodic;                  /* true=周期，false=单次 */
    bool running;                   /* 是否已启动 */
    bool pending;                   /* 已到期、等待回调任务执行 */
    uint32_t deadline;              /* 下次到期时刻 */
};

/*
 * 定时器回调执行上下文：
 *   - IOT_USE_TIMER_TASK=1（默认）：专用内核任务 "timer"，回调可使用阻塞 API；
 *   - IOT_USE_TIMER_TASK=0：PC 空闲任务 / STM32 SysTick 中断，回调不得阻塞。
 * 无论哪种模式，回调都应短小，避免长时间占用 CPU。
 */
int iot_timer_init(iot_timer_t *timer, const char *name,
                   iot_timer_cb_t cb, void *arg,
                   uint32_t period_ms, bool periodic);
int iot_timer_start(iot_timer_t *timer, uint32_t period_ms);
int iot_timer_stop(iot_timer_t *timer);

/* 动态创建/销毁：对象由 iot_mem_* 分配，失败返回 NULL */
iot_timer_t *iot_timer_create(const char *name, iot_timer_cb_t cb, void *arg,
                              uint32_t period_ms, bool periodic);

/* 销毁动态定时器：先停止并摘链，再释放内存 */
int iot_timer_destroy(iot_timer_t *timer);

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
