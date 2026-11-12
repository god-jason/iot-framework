#ifndef ROS_IDLE_H
#define ROS_IDLE_H

/*
 * ros_idle.h - 内置空闲任务（idle.c）
 *
 * 固定为最低优先级，永远就绪；无其它任务可运行时调用
 * ros_port_idle() 推进模拟时间或进入低功耗。
 */

#include "ros_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 内置空闲任务（idle.c 维护） */
extern ros_task_t *ros_idle_task;

/* 创建内置空闲任务 */
void ros_idle_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ROS_IDLE_H */
