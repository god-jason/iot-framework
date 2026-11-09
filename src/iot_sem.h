#ifndef IOT_SEM_H
#define IOT_SEM_H

/*
 * iot_sem.h - 计数信号量（iot_sem.c）
 *
 * take 在计数为 0 时按 timeout 阻塞；give 直接交接给最高优先级等待者。
 */

#include "iot_types.h"
#include "iot_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iot_sem {
    uint32_t count;                 /* 当前计数值（上限 IOT_SEM_MAX_COUNT） */
    iot_wait_queue_t wait;          /* 等待该信号量的任务 */
    bool initialized;               /* 初始化标志 */
};

int iot_sem_init(iot_sem_t *sem, uint32_t initial_count);
int iot_sem_take(iot_sem_t *sem, uint32_t timeout);
int iot_sem_give(iot_sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SEM_H */
