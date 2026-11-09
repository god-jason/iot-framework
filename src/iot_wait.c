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

/* 求最低置位编号，返回 -1 表示 0 */
static int iot_wait_ctz32(uint32_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    return (v == 0U) ? -1 : __builtin_ctz(v);
#else
    int n = 0;

    if (v == 0U) {
        return -1;
    }
    while ((v & 1U) == 0U) {
        v >>= 1;
        n++;
    }
    return n;
#endif
}

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

void iot_wait_queue_remove(iot_wait_queue_t *queue, iot_task_t *task)
{
    uint32_t prio;

    if (queue == NULL || task == NULL || task->wait_queue != queue) {
        return;
    }

    prio = task->wait_node.sort_key;
    if (prio < IOT_MAX_PRIORITY) {
        list_remove(&task->wait_node);
        if (list_empty(&queue->prio[prio])) {
            queue->bitmap &= ~(1U << prio);
        }
    }
    task->wait_queue = NULL;
}

iot_task_t *iot_wait_queue_pop_highest(iot_wait_queue_t *queue)
{
    int prio;
    list_node_t *node;
    iot_task_t *task;

    if (queue == NULL) {
        return NULL;
    }

    prio = iot_wait_ctz32(queue->bitmap);
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

uint8_t iot_wait_queue_top_priority(const iot_wait_queue_t *queue)
{
    int prio;

    if (queue == NULL) {
        return 0xFFU;
    }

    prio = iot_wait_ctz32(queue->bitmap);
    return (prio < 0) ? 0xFFU : (uint8_t)prio;
}

bool iot_wait_queue_contains(const iot_wait_queue_t *queue,
                             const iot_task_t *task)
{
    return (queue != NULL && task != NULL && task->wait_queue == queue);
}

uint8_t iot_wait_queue_first_prio(uint32_t bitmap)
{
    int prio = iot_wait_ctz32(bitmap);

    return (prio < 0) ? 0xFFU : (uint8_t)prio;
}

/* ------------------------------ 唤醒 / 阻塞 ------------------------------ */

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

    LOG_D(IOT_TAG, "wake: task=%s result=%d",
              task->name != NULL ? task->name : "?", (int)result);
}

int iot_block_current(iot_wait_queue_t *wait_queue, uint32_t timeout)
{
    iot_task_t *cur = iot_current_task;
    iot_task_t *next;

    if (cur == NULL || wait_queue == NULL) {
        return IOT_NOT_READY;
    }

    iot_port_critical_enter();
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

    LOG_D(IOT_TAG, "block: task=%s timeout=%u",
              cur->name != NULL ? cur->name : "?", (unsigned)timeout);

    iot_sched_switch(next);    /* 切走；被唤醒后从这里返回 */

    return cur->wait_result;
}
