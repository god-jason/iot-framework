#ifndef IOT_SEM_H
#define IOT_SEM_H

/*
 * sem.h - 计数信号量（sem.c）
 *
 * take 在计数为 0 时按 timeout 阻塞；give 直接交接给最高优先级等待者。
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

iot_status_t iot_sem_init(iot_sem_t *sem, uint32_t initial_count);
iot_status_t iot_sem_take(iot_sem_t *sem, uint32_t timeout);
iot_status_t iot_sem_give(iot_sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SEM_H */
