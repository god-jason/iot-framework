#include "iot_scheduler.h"
#include "iot_string.h"

#define IOT_TASK_MAX 16

static iot_task_t *s_tasks[IOT_TASK_MAX];
static iot_tick_t s_tick = 0;

int iot_scheduler_init(void)
{
    int i;
    for (i = 0; i < IOT_TASK_MAX; i++) {
        s_tasks[i] = NULL;
    }
    s_tick = 0;
    return IOT_OK;
}

int iot_scheduler_add(iot_task_t *task)
{
    int i;
    if (task == NULL || task->callback == NULL) {
        return -1;
    }
    for (i = 0; i < IOT_TASK_MAX; i++) {
        if (s_tasks[i] == NULL) {
            s_tasks[i] = task;
            break;
        }
    }
    if (i == IOT_TASK_MAX) {
        return -1;
    }
    task->state = IOT_TASK_READY;
    task->next_run = 0;
    task->run_count = 0;
    task->error_count = 0;
    task->next = NULL;
    return IOT_OK;
}

int iot_scheduler_remove(iot_task_t *task)
{
    int i;
    if (task == NULL) {
        return -1;
    }
    for (i = 0; i < IOT_TASK_MAX; i++) {
        if (s_tasks[i] == task) {
            s_tasks[i] = NULL;
            task->state = IOT_TASK_DISABLED;
            return IOT_OK;
        }
    }
    return -1;
}

int iot_scheduler_enable(iot_task_t *task)
{
    if (task == NULL) {
        return -1;
    }
    task->state = IOT_TASK_READY;
    task->next_run = s_tick;
    return IOT_OK;
}

int iot_scheduler_disable(iot_task_t *task)
{
    if (task == NULL) {
        return -1;
    }
    task->state = IOT_TASK_DISABLED;
    return IOT_OK;
}

iot_task_t *iot_scheduler_find(const char *name)
{
    int i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < IOT_TASK_MAX; i++) {
        if (s_tasks[i] != NULL && s_tasks[i]->name != NULL &&
            iot_string_compare(s_tasks[i]->name, name) == 0) {
            return s_tasks[i];
        }
    }
    return NULL;
}

int iot_scheduler_clear(void)
{
    int i;
    for (i = 0; i < IOT_TASK_MAX; i++) {
        s_tasks[i] = NULL;
    }
    return IOT_OK;
}

int iot_scheduler_process(void)
{
    int i;

    s_tick++;

    for (i = 0; i < IOT_TASK_MAX; i++) {
        iot_task_t *t = s_tasks[i];
        int ret;

        if (t == NULL || t->state == IOT_TASK_DISABLED) {
            continue;
        }
        if (s_tick < t->next_run) {
            continue;
        }

        t->state = IOT_TASK_RUNNING;
        ret = t->callback(t->user);
        t->run_count++;

        if (ret != IOT_OK) {
            t->error_count++;
            t->state = IOT_TASK_ERROR;
        } else {
            t->state = IOT_TASK_READY;
        }

        if (t->period > 0) {
            t->next_run = s_tick + t->period;
        } else {
            /* 一次性任务 */
            t->state = IOT_TASK_SLEEPING;
            t->next_run = 0;
        }
    }
    return IOT_OK;
}