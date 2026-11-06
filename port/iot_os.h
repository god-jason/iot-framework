#ifndef IOT_OS_H
#define IOT_OS_H

/*
 * OS 抽象接口。
 * 支持三种运行模式：Baremetal / FreeRTOS / 其他 RTOS。
 * 裸机模式下可不提供线程、信号量等能力，由主循环调用 iot_process()。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*iot_task_fn)(void *arg);

/* ---- 任务 ---- */
typedef struct {
    void *(*create)(iot_task_fn fn, void *arg, const char *name,
                    uint32_t stack_size, uint8_t priority);
    int (*delete)(void *task);
    void (*yield)(void);
} iot_os_task_ops_t;

/* ---- 互斥锁 ---- */
typedef struct {
    void *(*create)(void);
    int (*lock)(void *mutex, uint32_t timeout);
    int (*unlock)(void *mutex);
    int (*destroy)(void *mutex);
} iot_os_mutex_ops_t;

/* ---- 信号量 ---- */
typedef struct {
    void *(*create)(uint32_t max_count, uint32_t initial_count);
    int (*take)(void *sem, uint32_t timeout);
    int (*give)(void *sem);
    int (*destroy)(void *sem);
} iot_os_sem_ops_t;

/* ---- 消息队列 ---- */
typedef struct {
    void *(*create)(uint32_t item_size, uint32_t item_count);
    int (*send)(void *queue, const void *data, uint32_t timeout);
    int (*receive)(void *queue, void *data, uint32_t timeout);
    uint32_t (*count)(void *queue);
    int (*destroy)(void *queue);
} iot_os_queue_ops_t;

/* ---- 统一 OS 操作集 ---- */
typedef struct {
    iot_os_task_ops_t  task;
    iot_os_mutex_ops_t mutex;
    iot_os_sem_ops_t   semaphore;
    iot_os_queue_ops_t queue;
} iot_os_ops_t;

int iot_os_init(const iot_os_ops_t *ops);
const iot_os_ops_t *iot_os_get(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_OS_H */