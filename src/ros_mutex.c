/*
 * ros_mutex.c - 互斥量（可选优先级继承）
 *
 * 语义：
 *   - 空闲时 lock 立即取得所有权；
 *   - 被占用时按 timeout 阻塞，等待者按优先级取最高；
 *   - unlock 把所有权直接转交给最高优先级等待者，或置空；
 *   - 开启 ROS_USE_PRIORITY_INHERIT 时，等待者会把持有者临时提升到自己
 *     的优先级，并沿“持有者阻塞在另一个互斥量上”的链路递归传播。
 *
 * 限制：非递归（同任务重复 lock 返回 ROS_TIMEOUT，不会自死锁）；持有多个
 * 互斥量时，解锁按 base_priority 恢复，嵌套继承在下次竞争事件重新计算。
 */

#include "ros.h"
#include "ros_port.h"
#include "ros_mem.h"

/* 等待队列中的最高优先级（无等待者时返回 fallback），O(1) */
static uint8_t ros_mutex_wait_top(const ros_mutex_t *mutex, uint8_t fallback)
{
    uint8_t top = ros_wait_queue_top_priority(&mutex->wait);

    return (top < fallback) ? top : fallback;
}

#if ROS_USE_PRIORITY_INHERIT
/*
 * 沿阻塞链提升优先级：owner 被提升到 prio；若 owner 自己也在等另一个
 * 互斥量，则继续提升那个互斥量的持有者。深度上限防止环状等待死循环。
 */
static void ros_mutex_inherit_locked(ros_task_t *owner, uint8_t prio)
{
    int depth = 0;

    while (owner != NULL && prio < owner->priority && depth < 8) {
        ros_sched_change_priority_locked(owner, prio);

        owner = (owner->wait_mutex != NULL) ? owner->wait_mutex->owner : NULL;
        depth++;
    }
}
#endif

/* 初始化互斥量：持有者置空并初始化等待队列。 */
int ros_mutex_init(ros_mutex_t *mutex)
{
    if (mutex == NULL) {
        LOG_E(ROS_TAG, "mutex_init: null mutex");
        return ROS_INVALID_PARAM;
    }

    mutex->owner = NULL;
    ros_wait_queue_init(&mutex->wait);
    mutex->initialized = true;

    LOG_D(ROS_TAG, "mutex init");
    return ROS_OK;
}

/*
 * 加锁：空闲时立即取得所有权；被占用时按 timeout 阻塞，
 * 并在开启优先级继承时临时提升持有者优先级，抑制优先级反转。
 */
int ros_mutex_lock(ros_mutex_t *mutex, uint32_t timeout)
{
    ros_task_t *cur = ros_current_task;
    ros_task_t *owner;
    int rc;

    if (!ROS_OBJ_VALID(mutex)) {
        LOG_E(ROS_TAG, "mutex_lock: invalid mutex");
        return ROS_INVALID_PARAM;
    }
    if (cur == NULL) {
        return ROS_NOT_READY;
    }
    if (!ros_wait_blockable(timeout)) {
        return ROS_FORBIDDEN_IN_ISR;
    }

    ros_port_critical_enter();

    owner = mutex->owner;
    if (owner == NULL) {
        mutex->owner = cur;
        ros_port_critical_exit();
        ROS_TRACE_OBJ(ROS_EVT_MUTEX_LOCK, (uint32_t)(uintptr_t)mutex, 0U);
        return ROS_OK;
    }

    if (owner == cur) {
        /* 非递归：同任务重复加锁立即返回超时，避免自死锁 */
        ros_port_critical_exit();
        LOG_W(ROS_TAG, "mutex_lock: non-recursive, already owned");
        return ROS_TIMEOUT;
    }

#if ROS_USE_PRIORITY_INHERIT
    /* 高优先级等待者提升持有者，抑制优先级反转 */
    ros_mutex_inherit_locked(owner, cur->priority);
#endif

    if (timeout == ROS_TIMEOUT_NONE) {
        ros_port_critical_exit();
        return ROS_TIMEOUT;
    }

    /* 与 ros_block_current 等价，但由调用者持有临界区：
     * 「入等待链 + 选下一个任务」与 unlock 的所有权交接保持原子。 */
#if ROS_USE_PRIORITY_INHERIT
    cur->wait_mutex = mutex;
#endif

    ros_block_current_locked(&mutex->wait, timeout);

    rc = cur->wait_result;

    /* 收尾：清理继承链标记；超时则按剩余等待者重算持有者优先级 */
    ros_port_critical_enter();
#if ROS_USE_PRIORITY_INHERIT
    cur->wait_mutex = NULL;
#endif
    if (rc != ROS_OK && mutex->owner != NULL) {
#if ROS_USE_PRIORITY_INHERIT
        uint8_t target = ros_mutex_wait_top(mutex, mutex->owner->base_priority);

        ros_sched_change_priority_locked(mutex->owner, target);
#endif
    }
    ros_port_critical_exit();

    if (rc == ROS_OK) {
        /* v2=1：经历阻塞后由持有者转交获得 */
        ROS_TRACE_OBJ(ROS_EVT_MUTEX_LOCK, (uint32_t)(uintptr_t)mutex, 1U);
    }
    return rc;
}

/*
 * 解锁：校验当前任务是否为持有者；若有等待者则把所有权直接转交
 * 给最高优先级等待者，否则置空并恢复持有者原始优先级。
 */
int ros_mutex_unlock(ros_mutex_t *mutex)
{
    ros_task_t *task;

    if (!ROS_OBJ_VALID(mutex)) {
        LOG_E(ROS_TAG, "mutex_unlock: invalid mutex");
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    if (mutex->owner != ros_current_task) {
        ros_port_critical_exit();
        LOG_W(ROS_TAG, "mutex_unlock: not owner");
        return ROS_NOT_OWNER;
    }

#if ROS_USE_PRIORITY_INHERIT
    /* 释放前恢复持有者原始优先级 */
    if (mutex->owner->priority != mutex->owner->base_priority) {
        ros_sched_change_priority_locked(mutex->owner, mutex->owner->base_priority);
    }
#endif

    task = ros_wait_queue_pop_highest(&mutex->wait);
    if (task != NULL) {
        /* 所有权直接转交给最高优先级等待者 */
        mutex->owner = task;
        ros_task_wake(task, ROS_OK);
        ros_port_critical_exit();
        LOG_D(ROS_TAG, "mutex unlock: handoff to %s",
                  task->name != NULL ? task->name : "?");
        ROS_TRACE_OBJ(ROS_EVT_MUTEX_UNLOCK, (uint32_t)(uintptr_t)mutex, 1U);
    } else {
        mutex->owner = NULL;
        ros_port_critical_exit();
        LOG_D(ROS_TAG, "mutex unlock: released");
        ROS_TRACE_OBJ(ROS_EVT_MUTEX_UNLOCK, (uint32_t)(uintptr_t)mutex, 0U);
    }
    return ROS_OK;
}

/* 动态创建互斥量：对象由 ros_mem_calloc 分配，失败返回 NULL。 */
ros_mutex_t *ros_mutex_create(void)
{
    ros_mutex_t *mutex = (ros_mutex_t *)ros_mem_calloc(1U, sizeof(*mutex));

    if (mutex == NULL) {
        LOG_E(ROS_TAG, "mutex_create: no memory");
        return NULL;
    }
    if (ros_mutex_init(mutex) != ROS_OK) {
        ros_mem_free(mutex);
        return NULL;
    }
    return mutex;
}

/*
 * 销毁动态互斥量：被持有或仍有等待者时返回 ROS_BUSY；
 * 成功时标记失效并释放对象内存。
 */
int ros_mutex_destroy(ros_mutex_t *mutex)
{
    if (!ROS_OBJ_VALID(mutex)) {
        return ROS_INVALID_PARAM;
    }

    /* 持有者与等待者检查需与失效标记在同一临界区 */
    ros_port_critical_enter();
    if (mutex->owner != NULL || !ros_wait_queue_empty(&mutex->wait)) {
        ros_port_critical_exit();
        LOG_W(ROS_TAG, "mutex_destroy: in use");
        return ROS_BUSY;
    }
    mutex->initialized = false;
    ros_port_critical_exit();

    ros_mem_free(mutex);
    return ROS_OK;
}
