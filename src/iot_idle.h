#ifndef IOT_IDLE_H
#define IOT_IDLE_H

/*
 * iot_idle.h - 内置空闲任务（idle.c）
 *
 * 固定为最低优先级，永远就绪；无其它任务可运行时调用
 * iot_port_idle() 推进模拟时间或进入低功耗。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 内置空闲任务（idle.c 维护） */
extern iot_task_t *iot_idle_task;

/* 创建内置空闲任务 */
void iot_idle_init(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_IDLE_H */
