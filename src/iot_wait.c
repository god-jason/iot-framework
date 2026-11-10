/*
 * iot_wait.c - 阻塞与唤醒原语
 *
 * 等待队列按优先级分链，bitmap 记录非空链：
 *   - 入队 / 摘除 / 取最高优先级等待者均为 O(1)；
 *   - 任务被优先级继承提升后，摘除仍按「挂入时的优先级」回到原链，
 *     该值记录在 wait_node.sort_key 中。
 *
 * 任务可能同时位于：
 *   - 某个 iot_wait_queue_t（wait_node）；
 *   - 延时链表（delayed_node，仅在带超时阻塞时）。
 * 唤醒路径与超时路径都会同时摘除两者。
 */

#include "iot.h"
#include "iot_port.h"
#include "iot_assert.h"

/* ------------------------------ 等待队列 ------------------------------ */

void iot_wait_queue_init(iot_wait_queue_t *queue)
{
    int i;

    if (queue == NULL) {
        return;
    }
    for (i = 0; i < IOT_MAX_PRIORITY; i++) {
        list_init(&queue->prio[i]);
    }
    queue->bitmap = 0U;
}

/* 把任务按当前优先级挂入等待队列尾部，并置位对应优先级位。 */
void iot_wait_queue_add(iot_wait_queue_t *queue, iot_task_t *task)
{
    uint8_t prio;

    if (queue == NULL || task == NULL) {
        return;
    }

    prio = task->priority;
    if (prio >= IOT_MAX_PRIORITY) {
        prio = (uint8_t)IOT_PRIORITY_IDLE;
    }

    /* sort_key 保存挂入瞬间的优先级，供后续 O(1) 摘除使用 */
    task->wait_node.sort_key = prio;
    list_add_tail(&queue->prio[prio], &task->wait_node);
    queue->bitmap |= (1U << prio);
    task->wait_queue = queue;
}

/* 从等待队列摘除任务并维护位图；任务未挂入时安全返回。 */
void iot_wait_queue_remove(iot_wait_queue_t *queue, iot_task_t *task)
{
    uint32_t prio;

    if (queue == NULL || task == NULL || task->wait_queue != queue) {
        return;
    }

    prio = task->wait_node.sort_key;
    list_remove(&task->wait_node);   /* 对游离节点安全 */
    if (prio < IOT_MAX_PRIORITY && list_empty(&queue->prio[prio])) {
        queue->bitmap &= ~(1U << prio);
    }
    task->wait_queue = NULL;
}

/* 取出并摘除最高优先级等待者；空队列返回 NULL。 */
iot_task_t *iot_wait_queue_pop_highest(iot_wait_queue_t *queue)
{
    int prio;
    list_node_t *node;
    iot_task_t *task;

    if (queue == NULL) {
        return NULL;
    }

    prio = iot_ctz32(queue->bitmap);
    if (prio < 0) {
        return NULL;
    }

    node = list_pop_first(&queue->prio[prio]);
    if (node == NULL) {
        queue->bitmap &= ~(1U << (uint32_t)prio);
        IOT_ASSERT(0);          /* bitmap 与链表不一致 */
        return NULL;
    }

    if (list_empty(&queue->prio[prio])) {
        queue->bitmap &= ~(1U << (uint32_t)prio);
    }

    task = IOT_CONTAINER_OF(node, iot_task_t, wait_node);
    task->wait_queue = NULL;
    return task;
}

/* 返回最高等待优先级；空队列返回 0xFF。 */
uint8_t iot_wait_queue_top_priority(const iot_wait_queue_t *queue)
{
    int prio;

    if (queue == NULL) {
        return 0xFFU;
    }

    prio = iot_ctz32(queue->bitmap);
    return (prio < 0) ? 0xFFU : (uint8_t)prio;
}

/* 判断任务是否正挂在指定等待队列上（O(1) 指针比较）。 */
bool iot_wait_queue_contains(const iot_wait_queue_t *queue,
                             const iot_task_t *task)
{
    return (queue != NULL && task != NULL && task->wait_queue == queue);
}

/* 判断等待队列是否为空（无任何等待者）。 */
bool iot_wait_queue_empty(const iot_wait_queue_t *queue)
{
    return (queue == NULL) || (queue->bitmap == 0U);
}

/* 返回位图最低置位对应的优先级（事件组广播遍历用）。 */
uint8_t iot_wait_queue_first_prio(uint32_t bitmap)
{
    int prio = iot_ctz32(bitmap);

    return (prio < 0) ? 0xFFU : (uint8_t)prio;
}

/* 唤醒队列中的全部等待者（拥塞解除后让所有等待者重试）。 */
void iot_wait_queue_wake_all(iot_wait_queue_t *queue)
{
    iot_task_t *task;

    while ((task = iot_wait_queue_pop_highest(queue)) != NULL) {
        iot_task_wake(task, IOT_OK);
    }
}

/* ------------------------------ 唤醒 / 阻塞 ------------------------------ */

/* 中断上下文只允许非阻塞调用；违例时断言并让调用方直接返回错误。 */
bool iot_wait_blockable(uint32_t timeout)
{
    if (timeout == IOT_TIMEOUT_NONE) {
        return true;
    }
    if (iot_in_isr()) {
        IOT_ASSERT(!iot_in_isr());
        LOG_E(IOT_TAG, "blocking API called in ISR, timeout=%u",
                  (unsigned)timeout);
        return false;
    }
    return true;
}

/*
 * 唤醒任务：摘除等待/延时节点、写入唤醒结果并加入就绪表；
 * 若被唤醒任务优先级高于当前任务，则请求一次重调度（MCU 端置位 PendSV）。
 */
void iot_task_wake(iot_task_t *task, int result)
{
    if (task == NULL) {
        return;
    }

    iot_port_critical_enter();
    if (task->wait_queue != NULL) {
        iot_wait_queue_remove(task->wait_queue, task);
    }
    list_remove(&task->delayed_node);   /* 从延时/超时链表摘除 */
    task->wait_result = result;
    iot_make_ready(task);               /* 状态置 READY 并加入就绪表 */

    /*
     * 抢占：只要被唤醒的任务优先级高于当前任务，就请求一次重新调度。
     * 覆盖任务上下文（unlock/give）与 ISR 上下文（FromISR API / 定时器回调），
     * STM32 端置位 PendSV，中断退出即刻切换；PC 协作端为空实现。
     */
    if (iot_current_task != NULL &&
        task->priority < iot_current_task->priority) {
        iot_port_request_switch();
    }
    iot_port_critical_exit();

    IOT_TRACE(IOT_EVT_TASK_WAKE, (uint32_t)(uintptr_t)task, (uint32_t)result);

    LOG_D(IOT_TAG, "wake: task=%s result=%d",
              task->name != NULL ? task->name : "?", (int)result);
}

/*
 * 阻塞当前任务：挂入指定等待队列（可选超时），
 * 在同一临界区内完成入表与选任务，随后切换到下一个任务。
 * ISR 中调用会触发断言并返回 IOT_FORBIDDEN_IN_ISR。
 */
int iot_block_current(iot_wait_queue_t *wait_queue, uint32_t timeout)
{
    iot_task_t *cur = iot_current_task;

    if (cur == NULL || wait_queue == NULL) {
        return IOT_NOT_READY;
    }
    if (!iot_wait_blockable(timeout)) {
        return IOT_FORBIDDEN_IN_ISR;
    }

    IOT_TRACE(IOT_EVT_TASK_BLOCK, (uint32_t)(uintptr_t)cur, timeout);

    LOG_D(IOT_TAG, "block: task=%s timeout=%u",
              cur->name != NULL ? cur->name : "?", (unsigned)timeout);

    iot_port_critical_enter();
    iot_block_current_locked(wait_queue, timeout);

    return cur->wait_result;
}

/*
 * 临界区内版本的阻塞：完成状态切换、入等待链、挂延时表与选任务，
 * 退出临界区后执行实际切换。被唤醒后从 iot_sched_switch() 返回。
 */
void iot_block_current_locked(iot_wait_queue_t *wait_queue, uint32_t timeout)
{
    iot_task_t *cur = iot_current_task;
    iot_task_t *next;

    cur->state = IOT_TASK_BLOCKED;
    cur->wait_result = IOT_OK;
    iot_wait_queue_add(wait_queue, cur);

    if (timeout != IOT_TIMEOUT_FOREVER) {
        /* 带超时：同时挂入延时链表，到期由 tick 唤醒并返回 IOT_TIMEOUT */
        cur->wake_tick = iot_tick_count + timeout;
        iot_delayed_add(cur);
    }

    /*
     * 在同一个临界区内完成「阻塞入表 + 选出下一个任务」，
     * 避免解锁后、切换前被 tick 中断唤醒而重复进入就绪表。
     */
    next = iot_sched_pick();
    iot_port_critical_exit();

    iot_sched_switch(next);    /* 切走；被唤醒后从这里返回 */
}

/*
 * 带 deadline 语义的等待一步：虚假唤醒后按剩余时间续等，
 * 防止「被唤醒但条件仍不满足」时提前返回超时。
 */
int iot_wait_retry(iot_wait_queue_t *queue, uint32_t deadline, uint32_t timeout)
{
    uint32_t wait;

    if (timeout == IOT_TIMEOUT_NONE) {
        return IOT_TIMEOUT;
    }
    if (timeout == IOT_TIMEOUT_FOREVER) {
        wait = IOT_TIMEOUT_FOREVER;
    } else {
        uint32_t now = iot_tick_get();

        if (!IOT_TICK_AFTER(deadline, now)) {
            return IOT_TIMEOUT;
        }
        wait = deadline - now;
    }

    return iot_block_current(queue, wait);
}

/* 销毁对象前的统一收尾：等待者清空后标记失效。 */
int iot_obj_destroy_begin(bool *initialized,
                          iot_wait_queue_t *wait1,
                          iot_wait_queue_t *wait2)
{
    int rc = IOT_OK;

    iot_port_critical_enter();
    if (!iot_wait_queue_empty(wait1) || !iot_wait_queue_empty(wait2)) {
        rc = IOT_BUSY;
    } else {
        *initialized = false;
    }
    iot_port_critical_exit();

    return rc;
}
