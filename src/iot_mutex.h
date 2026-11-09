#ifndef IOT_MUTEX_H
#define IOT_MUTEX_H

/*
 * iot_mutex.h - 互斥量（iot_mutex.c）
 *
 * 非递归、带所有权校验；IOT_USE_PRIORITY_INHERIT 打开时实现优先级继承
 * （含沿阻塞链的递归传播），解锁时恢复持有者原始优先级。
 */

#include "iot_types.h"
#include "iot_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iot_mutex {
    iot_task_t *owner;             /* 当前持有者，NULL 表示空闲 */
    iot_wait_queue_t wait;         /* 等待该互斥量的任务 */
    bool initialized;               /* 初始化标志 */
};

int iot_mutex_init(iot_mutex_t *mutex);
int iot_mutex_lock(iot_mutex_t *mutex, uint32_t timeout);
int iot_mutex_unlock(iot_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MUTEX_H */
