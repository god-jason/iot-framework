/*
 * iot_sem.c - 计数信号量
 *
 * 语义：
 *   - iot_sem_take：计数 > 0 立即减一并返回；否则按 timeout 阻塞；
 *   - iot_sem_give：若有等待者，直接把令牌转交给最高优先级等待者
 *     （计数保持为 0），否则计数加一。
 */

#include "iot.h"
#include "iot_port.h"

int iot_sem_init(iot_sem_t *sem, uint32_t initial_count)
{
    if (sem == NULL) {
        LOG_E(IOT_TAG, "sem_init: null sem");
        return IOT_INVALID_PARAM;
    }

    sem->count = initial_count;
    iot_wait_queue_init(&sem->wait);
    sem->initialized = true;

    LOG_D(IOT_TAG, "sem init: count=%u", (unsigned)initial_count);
    return IOT_OK;
}

int iot_sem_take(iot_sem_t *sem, uint32_t timeout)
{
    if (!IOT_OBJ_VALID(sem)) {
        LOG_E(IOT_TAG, "sem_take: invalid sem");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    if (sem->count > 0U) {
        sem->count--;
        iot_port_critical_exit();
        LOG_D(IOT_TAG, "sem take: ok, count=%u", (unsigned)sem->count);
        return IOT_OK;
    }
    iot_port_critical_exit();

    if (timeout == IOT_TIMEOUT_NONE) {
        LOG_D(IOT_TAG, "sem take: empty, non-blocking");
        return IOT_TIMEOUT;
    }

    LOG_D(IOT_TAG, "sem take: empty, blocking timeout=%u", (unsigned)timeout);
    return iot_block_current(&sem->wait, timeout);
}

int iot_sem_give(iot_sem_t *sem)
{
    iot_task_t *task;

    if (!IOT_OBJ_VALID(sem)) {
        LOG_E(IOT_TAG, "sem_give: invalid sem");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    task = iot_wait_queue_pop_highest(&sem->wait);
    if (task != NULL) {
        /* 有等待者：令牌直接转交，计数不变 */
        iot_task_wake(task, IOT_OK);
        iot_port_critical_exit();
        LOG_D(IOT_TAG, "sem give: handoff to %s",
                  task->name != NULL ? task->name : "?");
        return IOT_OK;
    }
    if (sem->count >= IOT_SEM_MAX_COUNT) {
        iot_port_critical_exit();
        LOG_W(IOT_TAG, "sem give: count overflow, max=%u",
                  (unsigned)IOT_SEM_MAX_COUNT);
        return IOT_OVERFLOW;
    }

    sem->count++;
    iot_port_critical_exit();

    LOG_D(IOT_TAG, "sem give: count=%u", (unsigned)sem->count);
    return IOT_OK;
}