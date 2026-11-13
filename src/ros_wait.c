/*
 * ros_wait.c - 阻塞与唤醒原语
 *
 * 等待队列按优先级分链，bitmap 记录非空链：
 *   - 入队 / 摘除 / 取最高优先级等待者均为 O(1)；
 *   - 任务被优先级继承提升后，摘除仍按「挂入时的优先级」回到原链，
 *     该值记录在 wait_node.sort_key 中。
 *
 * 任务可能同时位于：
 *   - 某个 ros_wait_queue_t（wait_node）；
 *   - 延时链表（delayed_node，仅在带超时阻塞时）。
 * 唤醒路径与超时路径都会同时摘除两者。
 */

#include "ros.h"
#include "common.h"
#include "ros_port.h"
#include "ros_assert.h"

/* ------------------------------ 等待队列 ------------------------------ */

void ros_wait_queue_init(ros_wait_queue_t *queue)
{
    int i;

    if (queue == NULL) {
        return;
    }
    for (i = 0; i < ROS_MAX_PRIORITY; i++) {
        list_init(&queue->prio[i]);
    }
    queue->bitmap = 0U;
}

/* 把任务按当前优先级挂入等待队列尾部，并置位对应优先级位。 */
void ros_wait_queue_add(ros_wait_queue_t *queue, ros_task_t *task)
{
    uint8_t prio;

    if (queue == NULL || task == NULL) {
        return;
    }

    prio = task->priority;
    if (prio >= ROS_MAX_PRIORITY) {
        prio = (uint8_t)ROS_PRIORITY_IDLE;
    }

    /* sort_key 保存挂入瞬间的优先级，供后续 O(1) 摘除使用 */
    task->wait_node.sort_key = prio;
    list_add_tail(&queue->prio[prio], &task->wait_node);
    queue->bitmap |= (1U << prio);
    task->wait_queue = queue;
}

/* 从等待队列摘除任务并维护位图；任务未挂入时安全返回。 */
void ros_wait_queue_remove(ros_wait_queue_t *queue, ros_task_t *task)
{
    uint32_t prio;

    if (queue == NULL || task == NULL || task->wait_queue != queue) {
        return;
    }

    prio = task->wait_node.sort_key;
    list_remove(&task->wait_node);   /* 对游离节点安全 */
    if (prio < ROS_MAX_PRIORITY && list_empty(&queue->prio[prio])) {
        queue->bitmap &= ~(1U << prio);
    }
    task->wait_queue = NULL;
}

/* 取出并摘除最高优先级等待者；空队列返回 NULL。 */
ros_task_t *ros_wait_queue_pop_highest(ros_wait_queue_t *queue)
{
    int prio;
    list_node_t *node;
    ros_task_t *task;

    if (queue == NULL) {
        return NULL;
    }

    prio = ros_ctz32(queue->bitmap);
    if (prio < 0) {
        return NULL;
    }

    node = list_pop_first(&queue->prio[prio]);
    if (node == NULL) {
        queue->bitmap &= ~(1U << (uint32_t)prio);
        ROS_ASSERT(0);          /* bitmap 与链表不一致 */
        return NULL;
    }

    if (list_empty(&queue->prio[prio])) {
        queue->bitmap &= ~(1U << (uint32_t)prio);
    }

    task = ROS_CONTAINER_OF(node, ros_task_t, wait_node);
    task->wait_queue = NULL;
    return task;
}

/* 返回最高等待优先级；空队列返回 0xFF。 */
uint8_t ros_wait_queue_top_priority(const ros_wait_queue_t *queue)
{
    int prio;

    if (queue == NULL) {
        return 0xFFU;
    }

    prio = ros_ctz32(queue->bitmap);
    return (prio < 0) ? 0xFFU : (uint8_t)prio;
}

/* 判断任务是否正挂在指定等待队列上（O(1) 指针比较）。 */
bool ros_wait_queue_contains(const ros_wait_queue_t *queue,
                             const ros_task_t *task)
{
    return (queue != NULL && task != NULL && task->wait_queue == queue);
}

/* 判断等待队列是否为空（无任何等待者）。 */
bool ros_wait_queue_empty(const ros_wait_queue_t *queue)
{
    return (queue == NULL) || (queue->bitmap == 0U);
}

/* 返回位图最低置位对应的优先级（事件组广播遍历用）。 */
uint8_t ros_wait_queue_first_prio(uint32_t bitmap)
{
    int prio = ros_ctz32(bitmap);

    return (prio < 0) ? 0xFFU : (uint8_t)prio;
}

/* 唤醒队列中的全部等待者（拥塞解除后让所有等待者重试）。 */
void ros_wait_queue_wake_all(ros_wait_queue_t *queue)
{
    ros_task_t *task;

    while ((task = ros_wait_queue_pop_highest(queue)) != NULL) {
        ros_task_wake(task, ROS_OK);
    }
}

/* ------------------------------ 唤醒 / 阻塞 ------------------------------ */

/* 中断上下文只允许非阻塞调用；违例时断言并让调用方直接返回错误。 */
bool ros_wait_blockable(uint32_t timeout)
{
    if (timeout == ROS_TIMEOUT_NONE) {
        return true;
    }
    if (ros_in_isr()) {
        ROS_ASSERT(!ros_in_isr());
        LOG_E(ROS_TAG, "blocking API called in ISR, timeout=%u",
                  (unsigned)timeout);
        return false;
    }
    return true;
}

/*
 * 唤醒任务：摘除等待/延时节点、写入唤醒结果并加入就绪表；
 * 若被唤醒任务优先级高于当前任务，则请求一次重调度（MCU 端置位 PendSV）。
 */
void ros_task_wake(ros_task_t *task, int result)
{
    bool preempt = false;

    if (task == NULL) {
        return;
    }

    ros_port_critical_enter();
    if (task->wait_queue != NULL) {
        ros_wait_queue_remove(task->wait_queue, task);
    }
    list_remove(&task->delayed_node);   /* 从延时/超时链表摘除 */
    task->wait_result = result;
    ros_make_ready(task);               /* 状态置 READY 并加入就绪表 */

    /*
     * 抢占：只要被唤醒的任务优先级高于当前任务，就请求一次重新调度。
     * 覆盖任务上下文（unlock/give）与 ISR 上下文（FromISR API / 定时器回调），
     * STM32 端置位 PendSV，中断退出即刻切换；PC 协作端为空实现。
     */
    if (ros_current_task != NULL &&
        task->priority < ros_current_task->priority) {
        preempt = true;
        ros_port_request_switch();
    }
    ros_port_critical_exit();

    if (preempt) {
        ROS_TRACE(ROS_EVT_PREEMPT, (uint32_t)(uintptr_t)task,
                  (uint32_t)(uintptr_t)ros_current_task);
    }
    ROS_TRACE(ROS_EVT_TASK_WAKE, (uint32_t)(uintptr_t)task, (uint32_t)result);

    LOG_D(ROS_TAG, "wake: task=%s result=%d",
              task->name != NULL ? task->name : "?", (int)result);
}

/*
 * 阻塞当前任务：挂入指定等待队列（可选超时），
 * 在同一临界区内完成入表与选任务，随后切换到下一个任务。
 * ISR 中调用会触发断言并返回 ROS_FORBIDDEN_IN_ISR。
 */
int ros_block_current(ros_wait_queue_t *wait_queue, uint32_t timeout)
{
    ros_task_t *cur = ros_current_task;

    if (cur == NULL || wait_queue == NULL) {
        return ROS_NOT_READY;
    }
    if (!ros_wait_blockable(timeout)) {
        return ROS_FORBIDDEN_IN_ISR;
    }

    ROS_TRACE(ROS_EVT_TASK_BLOCK, (uint32_t)(uintptr_t)cur, timeout);

    LOG_D(ROS_TAG, "block: task=%s timeout=%u",
              cur->name != NULL ? cur->name : "?", (unsigned)timeout);

    ros_port_critical_enter();
    ros_block_current_locked(wait_queue, timeout);

    return cur->wait_result;
}

/*
 * 临界区内版本的阻塞：完成状态切换、入等待链、挂延时表与选任务，
 * 退出临界区后执行实际切换。被唤醒后从 ros_sched_switch() 返回。
 */
void ros_block_current_locked(ros_wait_queue_t *wait_queue, uint32_t timeout)
{
    ros_task_t *cur = ros_current_task;
    ros_task_t *next;

    cur->state = ROS_TASK_BLOCKED;
    cur->wait_result = ROS_OK;
    ros_wait_queue_add(wait_queue, cur);

    if (timeout != ROS_TIMEOUT_FOREVER) {
        /* 带超时：同时挂入延时链表，到期由 tick 唤醒并返回 ROS_TIMEOUT */
        cur->wake_tick = ros_tick_count + timeout;
        ros_delayed_add(cur);
    }

    /*
     * 在同一个临界区内完成「阻塞入表 + 选出下一个任务」，
     * 避免解锁后、切换前被 tick 中断唤醒而重复进入就绪表。
     */
    next = ros_sched_pick();
    ros_port_critical_exit();

    ros_sched_switch(next);    /* 切走；被唤醒后从这里返回 */
}

/*
 * 带 deadline 语义的等待一步：虚假唤醒后按剩余时间续等，
 * 防止「被唤醒但条件仍不满足」时提前返回超时。
 */
int ros_wait_retry(ros_wait_queue_t *queue, uint32_t deadline, uint32_t timeout)
{
    uint32_t wait;

    if (timeout == ROS_TIMEOUT_NONE) {
        return ROS_TIMEOUT;
    }
    if (timeout == ROS_TIMEOUT_FOREVER) {
        wait = ROS_TIMEOUT_FOREVER;
    } else {
        uint32_t now = ros_tick_get();

        if (!ROS_TICK_AFTER(deadline, now)) {
            return ROS_TIMEOUT;
        }
        wait = deadline - now;
    }

    return ros_block_current(queue, wait);
}

/* 销毁对象前的统一收尾：等待者清空后标记失效。 */
int ros_obj_destroy_begin(bool *initialized,
                          ros_wait_queue_t *wait1,
                          ros_wait_queue_t *wait2)
{
    int rc = ROS_OK;

    ros_port_critical_enter();
    if (!ros_wait_queue_empty(wait1) || !ros_wait_queue_empty(wait2)) {
        rc = ROS_BUSY;
    } else {
        *initialized = false;
    }
    ros_port_critical_exit();

    return rc;
}
