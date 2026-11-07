#ifndef IOT_SCHEDULER_H
#define IOT_SCHEDULER_H

/*
 * 应用级任务调度器（独立于 RTOS 线程调度）。
 * 每个 iot_scheduler_process() 前进 1 tick，周期性执行任务。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_TASK_READY,
    IOT_TASK_RUNNING,
    IOT_TASK_SLEEPING,
    IOT_TASK_DISABLED,
    IOT_TASK_ERROR
} iot_task_state_t;

typedef struct iot_task iot_task_t;

typedef int (*iot_task_fn)(void *user);

struct iot_task {
    const char *name;

    iot_task_fn callback;
    void *user;

    uint32_t period;    /* 周期（tick），0 表示一次性任务 */
    uint32_t next_run;  /* 下次执行 tick（内部使用） */
    uint8_t  priority;

    iot_task_state_t state;

    uint32_t run_count;
    uint32_t error_count;

    void *next;         /* 保留字段，当前未使用 */
};

int iot_scheduler_init(void);

int iot_scheduler_add(iot_task_t *task);
int iot_scheduler_remove(iot_task_t *task);

int iot_scheduler_enable(iot_task_t *task);
int iot_scheduler_disable(iot_task_t *task);

int iot_scheduler_process(void);
int iot_scheduler_clear(void);

iot_task_t *iot_scheduler_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SCHEDULER_H */