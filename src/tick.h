#ifndef IOT_TICK_H
#define IOT_TICK_H

/*
 * tick.h - 内核 tick 与延时链表（tick.c）
 *
 * tick 为 uint32 毫秒计数，约 49.7 天回绕；
 * 所有时间比较必须使用下方回绕安全宏。
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * tick 回绕安全比较：直接比较大小会在回绕点出错，
 * 统一用有符号差值判断（要求两个时刻处于同一个 2^31 tick 窗口）。
 */
#define IOT_TICK_AFTER(a, b)  ((int32_t)((a) - (b)) > 0)
#define IOT_TICK_BEFORE(a, b) ((int32_t)((a) - (b)) < 0)

/* 当前 tick 计数（由 tick.c 维护） */
extern uint32_t iot_tick_count;

/* 获取内核 tick（毫秒累计值，允许回绕） */
uint32_t iot_tick_get(void);

/* 内核 tick 推进 1（唤醒到期任务、触发定时器）；单片机由 SysTick 中断调用 */
void iot_tick(void);

/*
 * 批量推进 delta 个 tick。
 * 仅用于 PC 模拟端/测试，单片机端请通过 iot_tick() 由 SysTick 驱动。
 */
void iot_tick_increment(uint32_t delta);

/* ---------------- 内核内部接口 ---------------- */

/* 初始化 tick 与延时链表 */
void iot_tick_init(void);

/*
 * 把任务按 wake_tick 插入延时链表。
 * 注意：调用者必须已进入临界区；本函数不修改任务状态。
 */
void iot_delayed_add(iot_task_t *task);

/* 延时链表最早唤醒时刻；空链表返回 UINT32_MAX */
uint32_t iot_delayed_next_deadline(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_TICK_H */
