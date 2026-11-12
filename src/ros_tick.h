#ifndef ROS_TICK_H
#define ROS_TICK_H

/*
 * ros_tick.h - 内核 tick 与延时链表（tick.c）
 *
 * 时间单位由移植层决定（默认 1 tick = 1 ms）。
 */

#include "ros_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 内核 tick 计数（32 位，回绕安全） */
extern uint32_t ros_tick_count;

/*
 * 判断 a 是否在 b 之后（处理 32 位回绕）。
 * 要求两个时刻落在同一个 2^31 tick 窗口内。
 */
#define ROS_TICK_AFTER(a, b) ((int32_t)((uint32_t)(a) - (uint32_t)(b)) > 0)

/* 初始化 tick 与延时链表 */
void ros_tick_init(void);

/* 把任务挂入延时链表（调用者需已进入临界区，且已填 wake_tick） */
void ros_delayed_add(ros_task_t *task);

/* 延时链表最早到期时刻；空链表返回 UINT32_MAX */
uint32_t ros_delayed_next_deadline(void);

/*
 * 推进 delta 个 tick。
 * 到期任务先在临界区内摘链，退出临界区后再逐个唤醒，缩短关中断时间。
 */
void ros_tick_increment(uint32_t delta);

/* 推进 1 个 tick（SysTick 常规路径） */
void ros_tick(void);

/* 当前 tick 值 */
uint32_t ros_tick_get(void);

/*
 * 距离下一个唤醒/定时截止点还有多少 tick（用于 tickless 空闲）。
 * 无任何截止点返回 UINT32_MAX；已到期返回 1。
 */
uint32_t ros_idle_next_timeout(void);

#ifdef __cplusplus
}
#endif

#endif /* ROS_TICK_H */
