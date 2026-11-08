/*
 * sem.c - 计数信号量
 *
 * 语义：
 *   - iot_sem_take：计数 > 0 立即减一并返回；否则按 timeout 阻塞；
 *   - iot_sem_give：若有等待者，直接把令牌转交给最高优先级等待者
 *     （计数保持为 0），否则计数加一。
 */

#include "internal.h"
#include "port.h"

iot_status_t iot_sem_init(iot_sem_t *sem, uint32_t initial_count)
{
    if (sem == NULL) {
        LOG_E("iot", "sem_init: null sem");
        return IOT_INVALID_PARAM;
    }

    sem->count = initial_count;
    list_init(&sem->wait);
    sem->initialized = true;

    LOG_D("iot", "sem init: count=%u", (unsigned)initial_count);
    return IOT_OK;
}

iot_status_t iot_sem_take(iot_sem_t *sem, uint32_t timeout)
{
    if (sem == NULL || !sem->initialized) {
        LOG_E("iot", "sem_take: invalid sem");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    if (sem->count > 0U) {
        sem->count--;
        iot_port_critical_exit();
        LOG_D("iot", "sem take: ok, count=%u", (unsigned)sem->count);
        return IOT_OK;
    }
    iot_port_critical_exit();

    if (timeout == IOT_TIMEOUT_NONE) {
        LOG_D("iot", "sem take: empty, non-blocking");
        return IOT_TIMEOUT;
    }

    LOG_D("iot", "sem take: empty, blocking timeout=%u", (unsigned)timeout);
    return iot_block_current(&sem->wait, timeout);
}

iot_status_t iot_sem_give(iot_sem_t *sem)
{
    iot_task_t *task;

    if (sem == NULL || !sem->initialized) {
        LOG_E("iot", "sem_give: invalid sem");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    task = iot_wait_pop_highest(&sem->wait);
    if (task != NULL) {
        /* 有等待者：令牌直接转交，计数不变 */
        iot_task_wake(task, IOT_OK);
        iot_port_critical_exit();
        LOG_D("iot", "sem give: handoff to %s",
                  task->name != NULL ? task->name : "?");
        return IOT_OK;
    }
    sem->count++;
    iot_port_critical_exit();

    LOG_D("iot", "sem give: count=%u", (unsigned)sem->count);
    return IOT_OK;
}