#ifndef IOT_SCHED_H
#define IOT_SCHED_H

/*
 * iot_sched.h - 就绪表与调度器（sched.c）
 *
 * 固定优先级：就绪位图 + 每优先级一条环形链表，取值最小者为最高优先级。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 当前运行任务（sched.c 维护） */
extern iot_task_t *iot_current_task;

/* 获取当前运行任务，可能为 NULL（尚未启动） */
iot_task_t *iot_current(void);

/* 初始化就绪表与当前任务指针 */
void iot_sched_init(void);

/* 把任务加入就绪表（尾插 + 置位对应优先级），状态置 READY */
void iot_make_ready(iot_task_t *task);

/* 取出最高优先级就绪任务（出表并置 RUNNING）；无就绪任务返回 NULL */
iot_task_t *iot_sched_select(void);

/*
 * 在临界区内选出下一个要运行的任务（无就绪任务时返回空闲任务）。
 * 与 iot_sched_switch() 配对使用，保证“状态更新 + 选择”原子完成。
 */
iot_task_t *iot_sched_pick(void);

/* 临界区外执行实际切换；next 为 NULL 或与当前任务相同时直接返回 */
void iot_sched_switch(iot_task_t *next);

/* 执行一次调度：选择最高优先级任务并切换 */
void iot_sched(void);

/*
 * 中断/PendSV 上下文调度：只做状态迁移并返回要切入的任务。
 * 返回 NULL 表示无需切换；实际寄存器切换由移植层完成。
 */
iot_task_t *iot_sched_from_isr(void);

/*
 * 在临界区内调整任务优先级（优先级继承使用）。
 * 调用者必须已持有临界区；任务在就绪表上时会同步换链并维护位图。
 */
void iot_sched_change_priority_locked(iot_task_t *task, uint8_t new_prio);

/* 带临界区保护的优先级调整 */
void iot_sched_change_priority(iot_task_t *task, uint8_t new_prio);

#if IOT_USE_TIME_SLICING
/* tick 内调用（临界区内）：按 delta 消耗当前任务时间片，耗尽时置切换标志 */
void iot_sched_tick_slice(uint32_t delta);
#endif

/* 时间片是否已耗尽（移植层据此触发 PendSV） */
bool iot_sched_slice_expired(void);

#if IOT_USE_STATS
/* 上下文切换总次数 */
uint32_t iot_sched_switch_count(void);

/* 清零上下文切换计数 */
void iot_sched_reset_switch_count(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IOT_SCHED_H */
