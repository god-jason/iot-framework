#ifndef IOT_TASK_H
#define IOT_TASK_H

/*
 * task.h - 任务管理（task.c）
 *
 * 创建、让出、睡眠与退出。
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 让出 CPU，触发同优先级/高优先级调度 */
void iot_yield(void);

/*
 * 创建任务。
 *   task       : 任务控制块（调用者静态提供）；
 *   name       : 任务名（只读引用）；
 *   priority   : 优先级 0..IOT_PRIORITY_IDLE-1；
 *   entry      : 任务入口，返回后自动退出；
 *   arg        : 入口参数；
 *   stack_mem  : 任务栈内存（调用者提供，建议 8 字节对齐）；
 *   stack_size : 栈大小（字节）。
 */
iot_status_t iot_task_create(iot_task_t *task, const char *name,
                               uint8_t priority, iot_entry_t entry, void *arg,
                               void *stack_mem, uint32_t stack_size);

/* 退出当前任务（任务主体返回后由移植层自动调用） */
void iot_task_exit(void);

/* 睡眠 ms 个 tick，让出 CPU 直到到期唤醒 */
void iot_sleep_ms(uint32_t ms);

/* 睡眠到绝对 tick 时刻 */
void iot_sleep_until(uint32_t wake_tick);

#ifdef __cplusplus
}
#endif

#endif /* IOT_TASK_H */
