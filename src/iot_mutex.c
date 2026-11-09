/*
 * iot_mutex.c - 互斥量（可选优先级继承）
 *
 * 语义：
 *   - 空闲时 lock 立即取得所有权；
 *   - 被占用时按 timeout 阻塞，等待者按优先级取最高；
 *   - unlock 把所有权直接转交给最高优先级等待者，或置空；
 *   - 开启 IOT_USE_PRIORITY_INHERIT 时，等待者会把持有者临时提升到自己
 *     的优先级，并沿“持有者阻塞在另一个互斥量上”的链路递归传播。
 *
 * 限制：非递归（同任务重复 lock 返回 IOT_TIMEOUT，不会自死锁）；持有多个
 * 互斥量时，解锁按 base_priority 恢复，嵌套继承在下次竞争事件重新计算。
 */

#include "iot.h"
#include "iot_port.h"

/* 等待队列中的最高优先级（无等待者时返回 fallback），O(1) */
static uint8_t iot_mutex_wait_top(const iot_mutex_t *mutex, uint8_t fallback)
{
    uint8_t top = iot_wait_queue_top_priority(&mutex->wait);

    return (top < fallback) ? top : fallback;
}

#if IOT_USE_PRIORITY_INHERIT
/*
 * 沿阻塞链提升优先级：owner 被提升到 prio；若 owner 自己也在等另一个
 * 互斥量，则继续提升那个互斥量的持有者。深度上限防止环状等待死循环。
 */
static void iot_mutex_inherit_locked(iot_task_t *owner, uint8_t prio)
{
    int depth = 0;

    while (owner != NULL && prio < owner->priority && depth < 8) {
        iot_sched_change_priority_locked(owner, prio);

        owner = (owner->wait_mutex != NULL) ? owner->wait_mutex->owner : NULL;
        depth++;
    }
}
#endif

int iot_mutex_init(iot_mutex_t *mutex)
{
    if (mutex == NULL) {
        LOG_E(IOT_TAG, "mutex_init: null mutex");
        return IOT_INVALID_PARAM;
    }

    mutex->owner = NULL;
    iot_wait_queue_init(&mutex->wait);
    mutex->initialized = true;

    LOG_D(IOT_TAG, "mutex init");
    return IOT_OK;
}

int iot_mutex_lock(iot_mutex_t *mutex, uint32_t timeout)
{
    iot_task_t *cur = iot_current_task;
    iot_task_t *owner;
    iot_task_t *next;
    int rc;

    if (!IOT_OBJ_VALID(mutex)) {
        LOG_E(IOT_TAG, "mutex_lock: invalid mutex");
        return IOT_INVALID_PARAM;
    }
    if (cur == NULL) {
        return IOT_NOT_READY;
    }

    iot_port_critical_enter();

    owner = mutex->owner;
    if (owner == NULL) {
        mutex->owner = cur;
        iot_port_critical_exit();
        return IOT_OK;
    }

    if (owner == cur) {
        /* 非递归：同任务重复加锁立即返回超时，避免自死锁 */
        iot_port_critical_exit();
        LOG_W(IOT_TAG, "mutex_lock: non-recursive, already owned");
        return IOT_TIMEOUT;
    }

#if IOT_USE_PRIORITY_INHERIT
    /* 高优先级等待者提升持有者，抑制优先级反转 */
    iot_mutex_inherit_locked(owner, cur->priority);
#endif

    if (timeout == IOT_TIMEOUT_NONE) {
        iot_port_critical_exit();
        return IOT_TIMEOUT;
    }

    /* 与 iot_block_current 等价，但把“入等待链 + 选择下一个任务”
     * 放在同一个临界区内，避免与 unlock 的所有权交接竞态。 */
    cur->state = IOT_TASK_BLOCKED;
    cur->wait_result = IOT_OK;
    iot_wait_queue_add(&mutex->wait, cur);

#if IOT_USE_PRIORITY_INHERIT
    cur->wait_mutex = mutex;
#endif

    if (timeout != IOT_TIMEOUT_FOREVER) {
        cur->wake_tick = iot_tick_count + timeout;
        iot_delayed_add(cur);
    }

    next = iot_sched_pick();
    iot_port_critical_exit();

    iot_sched_switch(next);          /* 切走；被唤醒后从这里返回 */

    rc = cur->wait_result;

    /* 收尾：清理继承链标记；超时则按剩余等待者重算持有者优先级 */
    iot_port_critical_enter();
#if IOT_USE_PRIORITY_INHERIT
    cur->wait_mutex = NULL;
#endif
    if (rc != IOT_OK && mutex->owner != NULL) {
#if IOT_USE_PRIORITY_INHERIT
        uint8_t target = iot_mutex_wait_top(mutex, mutex->owner->base_priority);

        iot_sched_change_priority_locked(mutex->owner, target);
#endif
    }
    iot_port_critical_exit();

    return rc;
}

int iot_mutex_unlock(iot_mutex_t *mutex)
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

#if IOT_USE_PRIORITY_INHERIT
    /* 释放前恢复持有者原始优先级 */
    if (mutex->owner->priority != mutex->owner->base_priority) {
        iot_sched_change_priority_locked(mutex->owner, mutex->owner->base_priority);
    }
#endif

    task = iot_wait_queue_pop_highest(&mutex->wait);
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
