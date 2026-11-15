/*
 * ros_sem.c - 计数信号量
 *
 * 语义：
 *   - ros_sem_take：计数 > 0 立即减一并返回；否则按 timeout 阻塞；
 *   - ros_sem_give：若有等待者，直接把令牌转交给最高优先级等待者
 *     （计数保持为 0），否则计数加一。
 */

#include "ros.h"
#include "common.h"
#include "ros_port.h"
#include "ros_mem.h"

/* 初始化计数信号量：写入初始计数并初始化等待队列。 */
int ros_sem_init(ros_sem_t *sem, uint32_t initial_count)
{
    if (sem == NULL) {
        LOG_E(ROS_TAG, "sem_init: null sem");
        return ROS_INVALID_PARAM;
    }

    sem->count = initial_count;
    ros_wait_queue_init(&sem->wait);
    sem->initialized = true;

    LOG_D(ROS_TAG, "sem init: count=%u", (unsigned)initial_count);
    return ROS_OK;
}

/*
 * 获取令牌：计数大于 0 时立即减一并返回；
 * 否则按 timeout 阻塞（ROS_TIMEOUT_NONE 表示不等待，立即返回超时）。
 *
 * 「检查计数 → 入等待链 → 选任务」在同一临界区内完成，杜绝取令牌方
 * 退出临界区后被 give 抢先增加计数而丢失唤醒、进而永久阻塞的竞态。
 */
int ros_sem_take(ros_sem_t *sem, uint32_t timeout)
{
    ros_task_t *cur;
    uint32_t deadline;
    uint32_t now;
    uint32_t wait;
    int rc;

    if (!ROS_OBJ_VALID(sem)) {
        LOG_E(ROS_TAG, "sem_take: invalid sem");
        return ROS_INVALID_PARAM;
    }
    if (!ros_wait_blockable(timeout)) {
        return ROS_FORBIDDEN_IN_ISR;
    }

    deadline = ros_tick_get() + timeout;

    ros_port_critical_enter();
    if (sem->count > 0U) {
        sem->count--;
        ros_port_critical_exit();
        ROS_TRACE_OBJ(ROS_EVT_SEM_TAKE, (uint32_t)(uintptr_t)sem, sem->count);
        LOG_D(ROS_TAG, "sem take: ok, count=%u", (unsigned)sem->count);
        return ROS_OK;
    }

    /* 计数为 0：在同一临界区内决定立即失败或阻塞 */
    if (timeout == ROS_TIMEOUT_NONE) {
        ros_port_critical_exit();
        LOG_D(ROS_TAG, "sem take: empty, non-blocking");
        return ROS_TIMEOUT;
    }

    cur = ros_current_task;
    if (cur == NULL) {
        ros_port_critical_exit();
        return ROS_NOT_READY;
    }
    now = ros_tick_get();
    if (timeout != ROS_TIMEOUT_FOREVER && !ROS_TICK_AFTER(deadline, now)) {
        ros_port_critical_exit();
        return ROS_TIMEOUT;
    }
    wait = (timeout == ROS_TIMEOUT_FOREVER) ? ROS_TIMEOUT_FOREVER
                                            : (deadline - now);

    LOG_D(ROS_TAG, "sem take: empty, blocking timeout=%u", (unsigned)timeout);
    ros_block_current_locked(&sem->wait, wait);
    rc = cur->wait_result;

    if (rc == ROS_OK) {
        /* give 已把令牌直接交接给本任务 */
        ROS_TRACE_OBJ(ROS_EVT_SEM_TAKE, (uint32_t)(uintptr_t)sem, sem->count);
        return ROS_OK;
    }
    LOG_D(ROS_TAG, "sem take: block timeout");
    return ROS_TIMEOUT;
}

/*
 * 释放令牌：有等待者时直接交接给最高优先级任务（计数保持 0）；
 * 否则计数加一，超过 ROS_SEM_MAX_COUNT 返回 ROS_OVERFLOW。
 */
int ros_sem_give(ros_sem_t *sem)
{
    ros_task_t *task;

    if (!ROS_OBJ_VALID(sem)) {
        LOG_E(ROS_TAG, "sem_give: invalid sem");
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    task = ros_wait_queue_pop_highest(&sem->wait);
    if (task != NULL) {
        /* 有等待者：令牌直接转交，计数不变 */
        ros_task_wake(task, ROS_OK);
        ros_port_critical_exit();
        LOG_D(ROS_TAG, "sem give: handoff to %s",
                  task->name != NULL ? task->name : "?");
        return ROS_OK;
    }
    if (sem->count >= ROS_SEM_MAX_COUNT) {
        ros_port_critical_exit();
        LOG_W(ROS_TAG, "sem give: count overflow, max=%u",
                  (unsigned)ROS_SEM_MAX_COUNT);
        return ROS_OVERFLOW;
    }

    sem->count++;
    ros_port_critical_exit();

    ROS_TRACE_OBJ(ROS_EVT_SEM_GIVE, (uint32_t)(uintptr_t)sem, sem->count);
    LOG_D(ROS_TAG, "sem give: count=%u", (unsigned)sem->count);
    return ROS_OK;
}

/* 动态创建信号量：对象由 ros_mem_calloc 分配，失败返回 NULL。 */
ros_sem_t *ros_sem_create(uint32_t initial_count)
{
    ros_sem_t *sem = (ros_sem_t *)ros_mem_calloc(1U, sizeof(*sem));

    if (sem == NULL) {
        LOG_E(ROS_TAG, "sem_create: no memory");
        return NULL;
    }
    if (ros_sem_init(sem, initial_count) != ROS_OK) {
        ros_mem_free(sem);
        return NULL;
    }
    return sem;
}

/*
 * 销毁动态信号量：仍有任务等待时返回 ROS_BUSY；
 * 成功时标记失效并释放对象内存。
 */
int ros_sem_destroy(ros_sem_t *sem)
{
    if (!ROS_OBJ_VALID(sem)) {
        return ROS_INVALID_PARAM;
    }

    if (ros_obj_destroy_begin(&sem->initialized, &sem->wait, NULL) != ROS_OK) {
        LOG_W(ROS_TAG, "sem_destroy: still has waiters");
        return ROS_BUSY;
    }

    ros_mem_free(sem);
    return ROS_OK;
}