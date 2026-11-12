#ifndef ROS_MUTEX_H
#define ROS_MUTEX_H

/*
 * ros_mutex.h - 互斥量（ros_mutex.c）
 *
 * 非递归、带所有权校验；ROS_USE_PRIORITY_INHERIT 打开时实现优先级继承
 * （含沿阻塞链的递归传播），解锁时恢复持有者原始优先级。
 */

#include "ros_types.h"
#include "ros_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ros_mutex {
    ros_task_t *owner;             /* 当前持有者，NULL 表示空闲 */
    ros_wait_queue_t wait;         /* 等待该互斥量的任务 */
    bool initialized;               /* 初始化标志 */
};

int ros_mutex_init(ros_mutex_t *mutex);
int ros_mutex_lock(ros_mutex_t *mutex, uint32_t timeout);
int ros_mutex_unlock(ros_mutex_t *mutex);

/* 动态创建/销毁：对象由 ros_mem_* 分配，失败返回 NULL */
ros_mutex_t *ros_mutex_create(void);

/* 销毁动态互斥量；被持有或仍有等待者时返回 ROS_BUSY */
int ros_mutex_destroy(ros_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* ROS_MUTEX_H */
