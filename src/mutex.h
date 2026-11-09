#ifndef IOT_MUTEX_H
#define IOT_MUTEX_H

/*
 * mutex.h - 轻量互斥量（mutex.c）
 *
 * 非递归、带所有权校验；不实现优先级继承。
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_mutex_init(iot_mutex_t *mutex);
int iot_mutex_lock(iot_mutex_t *mutex, uint32_t timeout);
int iot_mutex_unlock(iot_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MUTEX_H */
