/*
 * mutex.c - 轻量互斥量（不含优先级继承）
 *
 * 语义：
 *   - 空闲时 lock 立即取得所有权；
 *   - 被占用时按 timeout 阻塞，等待者按优先级取最高；
 *   - unlock 时把所有权直接转交给最高优先级等待者，或置空。
 *
 * 注意：本实现不处理优先级反转，实时性要求高的场景请谨慎使用。
 */

#include "iot.h"
#include "port.h"

iot_status_t iot_mutex_init(iot_mutex_t *mutex)
{
    if (mutex == NULL) {
        LOG_E(IOT_TAG, "mutex_init: null mutex");
        return IOT_INVALID_PARAM;
    }

    mutex->owner = NULL;
    list_init(&mutex->wait);
    mutex->initialized = true;

    LOG_D(IOT_TAG, "mutex init");
    return IOT_OK;
}

iot_status_t iot_mutex_lock(iot_mutex_t *mutex, uint32_t timeout)
{
    if (!IOT_OBJ_VALID(mutex)) {
        LOG_E(IOT_TAG, "mutex_lock: invalid mutex");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    if (mutex->owner == NULL) {
        mutex->owner = iot_current_task;
        iot_port_critical_exit();
        LOG_D(IOT_TAG, "mutex lock: acquired by %s",
                  (iot_current_task != NULL && iot_current_task->name != NULL)
                  ? iot_current_task->name : "?");
        return IOT_OK;
    }
    iot_port_critical_exit();

    if (timeout == IOT_TIMEOUT_NONE) {
        LOG_D(IOT_TAG, "mutex lock: busy, non-blocking");
        return IOT_TIMEOUT;
    }

    LOG_D(IOT_TAG, "mutex lock: busy, blocking timeout=%u", (unsigned)timeout);
    /* 成功后所有权已由 unlock 转交给当前任务 */
    return iot_block_current(&mutex->wait, timeout);
}

iot_status_t iot_mutex_unlock(iot_mutex_t *mutex)
{
    iot_task_t *task;

    if (!IOT_OBJ_VALID(mutex)) {
        LOG_E(IOT_TAG, "mutex_unlock: invalid mutex");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    if (mutex->owner != iot_current_task) {
        iot_port_critical_exit();
        LOG_W(IOT_TAG, "mutex_unlock: not owner");
        return IOT_NOT_OWNER;
    }

    task = iot_wait_pop_highest(&mutex->wait);
    if (task != NULL) {
        /* 所有权直接转交给最高优先级等待者 */
        mutex->owner = task;
        iot_task_wake(task, IOT_OK);
        iot_port_critical_exit();
        LOG_D(IOT_TAG, "mutex unlock: handoff to %s",
                  task->name != NULL ? task->name : "?");
    } else {
        mutex->owner = NULL;
        iot_port_critical_exit();
        LOG_D(IOT_TAG, "mutex unlock: released");
    }
    return IOT_OK;
}