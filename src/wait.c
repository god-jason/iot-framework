/*
 * wait.c - 阻塞与唤醒原语
 *
 * 信号量/互斥量/队列在无法立即满足需求时，统一调用 iot_block_current()
 * 把当前任务挂到对象自己的等待链表上；当条件满足时调用 iot_task_wake()。
 *
 * 任务可能同时位于：
 *   - 对象等待链表（wait_node）；
 *   - 延时链表（delayed_node，仅在带超时阻塞时）。
 * 唤醒路径与超时路径都会同时摘除两个节点（list_remove 幂等）。
 */

#include "internal.h"
#include "port.h"

iot_task_t *iot_wait_pop_highest(list_t *list)
{
    list_node_t *node;
    list_node_t *best = NULL;
    uint8_t best_prio = 0xFF;

    if (list == NULL) {
        return NULL;
    }

    /* 遍历等待队列，选择优先级数值最小（最高优先级）的任务 */
    for (node = list->head.next; node != &list->head; node = node->next) {
        iot_task_t *t = IOT_CONTAINER_OF(node, iot_task_t, wait_node);

        if (t->priority < best_prio) {
            best = node;
            best_prio = t->priority;
        }
    }

    if (best == NULL) {
        return NULL;
    }
    list_remove(best);
    return IOT_CONTAINER_OF(best, iot_task_t, wait_node);
}

void iot_task_wake(iot_task_t *task, iot_status_t result)
{
    if (task == NULL) {
        return;
    }

    iot_port_critical_enter();
    list_remove(&task->wait_node);     /* 从对象等待队列摘除 */
    list_remove(&task->delayed_node);  /* 从延时/超时链表摘除 */
    task->wait_result = result;
    iot_make_ready(task);                  /* 状态置 READY 并加入就绪表 */
    iot_port_critical_exit();

    LOG_D("iot", "wake: task=%s result=%d",
              task->name != NULL ? task->name : "?", (int)result);
}

iot_status_t iot_block_current(list_t *wait_list, uint32_t timeout)
{
    iot_task_t *cur = iot_current_task;

    if (cur == NULL || wait_list == NULL) {
        return IOT_NOT_READY;
    }

    iot_port_critical_enter();
    cur->state = IOT_TASK_BLOCKED;
    cur->wait_result = IOT_OK;
    list_add_tail(wait_list, &cur->wait_node);

    if (timeout != IOT_TIMEOUT_FOREVER) {
        /* 带超时：同时挂入延时链表，到期由 tick 唤醒并返回 IOT_TIMEOUT */
        cur->wake_tick = iot_tick_count + timeout;
        iot_delayed_add(cur);
    }
    iot_port_critical_exit();

    LOG_D("iot", "block: task=%s timeout=%u",
              cur->name != NULL ? cur->name : "?", (unsigned)timeout);

    iot_sched();               /* 切走；被唤醒后从这里返回 */

    return cur->wait_result;
}