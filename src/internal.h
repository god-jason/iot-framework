#ifndef IOT_INTERNAL_H
#define IOT_INTERNAL_H

/*
 * RTOS 内核内部声明（不对外公开）。
 *
 * 对象布局已在 types.h 完整公开；本文件只放：
 *   - 内核全局状态（当前任务、空闲任务、tick）；
 *   - 各 .c 之间共享的内部函数。
 *
 * 模块划分：
 *   sched.c  就绪表与调度器
 *   wait.c   阻塞/唤醒原语
 *   tick.c   tick 与延时链表
 *   idle.c   空闲任务
 *   sem.c / mutex.c / queue.c / timers.c / task.c
 */

#include "iot.h"
#include "log.h"

/* ============================== 内核全局状态 ============================== */

extern iot_task_t *iot_current_task;  /* sched.c：当前运行任务 */
extern iot_task_t *iot_idle_task;     /* idle.c ：内置空闲任务 */
extern uint32_t iot_tick_count;       /* tick.c ：内核 tick 计数 */

/* ============================== 就绪表 / 调度（sched.c） ============================== */

/* 初始化就绪表与当前任务指针 */
void iot_sched_init(void);

/* 把任务加入就绪表（尾插 + 置位对应优先级），状态置 READY */
void iot_make_ready(iot_task_t *task);

/* 取出最高优先级就绪任务（出表并置 RUNNING）；无就绪任务返回 NULL */
iot_task_t *iot_sched_select(void);

/* 执行一次调度：选择最高优先级任务并切换 */
void iot_sched(void);

/* ============================== 阻塞 / 唤醒（wait.c） ============================== */

/*
 * 将当前任务挂到 wait_list 并阻塞，然后触发调度。
 *   wait_list : 等待队列（wait_node）
 *   timeout   : IOT_TIMEOUT_FOREVER 永久等待；>0 时挂入延时链表，到期返回 IOT_TIMEOUT
 * 返回：被唤醒时写入的结果码（IOT_OK / IOT_TIMEOUT）。
 */
iot_status_t iot_block_current(list_t *wait_list, uint32_t timeout);

/* 唤醒任务：摘除等待/延时节点，写入结果并加入就绪表 */
void iot_task_wake(iot_task_t *task, iot_status_t result);

/* 从等待队列取出优先级最高的任务（摘除 wait_node）；无则返回 NULL */
iot_task_t *iot_wait_pop_highest(list_t *list);

/* ============================== tick / 延时链表（tick.c） ============================== */

/* 初始化 tick 与延时链表 */
void iot_tick_init(void);

/*
 * 把任务按 wake_tick 插入延时链表。
 * 注意：本函数不修改任务状态，调用者需先设置好 state。
 */
void iot_delayed_add(iot_task_t *task);

/* 延时链表最早唤醒时刻；空链表返回 UINT32_MAX */
uint32_t iot_delayed_next_deadline(void);

/* ============================== 空闲任务（idle.c） ============================== */

/* 创建内置空闲任务 */
void iot_idle_init(void);

/* 空闲钩子：推进模拟 tick 到最近唤醒/定时点（PC 模拟端使用） */
void iot_idle_hook(void);

/* ============================== 软件定时器（timers.c） ============================== */

/* 初始化定时器链表 */
void iot_timers_init(void);

/* 定时器链表最早到期时刻；空链表返回 UINT32_MAX */
uint32_t iot_timer_next_deadline(void);

/* 处理所有到期定时器（由 iot_tick_increment 调用） */
void iot_timer_tick(uint32_t now);

#endif /* IOT_INTERNAL_H */