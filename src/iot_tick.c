/*
 * iot_tick.c - 内核 tick 与延时链表
 *
 * s_delayed 为按 wake_tick 升序排列的延时链表：
 *   - 睡眠任务（IOT_TASK_DELAYED）；
 *   - 带超时阻塞的任务（IOT_TASK_BLOCKED，同时还在对象等待链表上）。
 *
 * 唤醒策略：
 *   到期任务先在临界区内摘链并暂存到局部链表，退出临界区后再逐个唤醒，
 *   把“关中断时间”从 O(到期任务数 × 链表操作) 降为 O(到期任务数)；
 *   同时累加运行统计并推进时间片。
 */

#include "iot.h"
#include "iot_port.h"

uint32_t iot_tick_count = 0;

static list_t s_delayed;

/* 延时链表节点 → 任务 */
static iot_task_t *iot_delayed_task(list_node_t *node)
{
    return IOT_CONTAINER_OF(node, iot_task_t, delayed_node);
}

void iot_tick_init(void)
{
    list_init(&s_delayed);
    iot_tick_count = 0U;
}

void iot_delayed_add(iot_task_t *task)
{
    list_node_t *node;
    list_node_t *tail;

    if (task == NULL) {
        return;
    }

    /* 调用者需已进入临界区（阻塞/睡眠路径均在同一临界区内调用） */
    node = &task->delayed_node;
    node->sort_key = task->wake_tick;

    /*
     * 常见场景是「新截止时间晚于队尾」（递增时间流），此时直接尾插为
     * O(1)；只有乱序插入才回退到 list_insert_sorted() 的线性查找。
     */
    tail = s_delayed.head.prev;
    if (tail == &s_delayed.head ||
        !IOT_TICK_AFTER(tail->sort_key, node->sort_key)) {
        list_add_tail(&s_delayed, node);
        return;
    }

    list_insert_sorted(&s_delayed, node);
}

uint32_t iot_delayed_next_deadline(void)
{
    list_node_t *node = list_first(&s_delayed);

    if (node == NULL) {
        return UINT32_MAX;
    }
    return iot_delayed_task(node)->wake_tick;
}

void iot_tick_increment(uint32_t delta)
{
    list_t expired;
    list_node_t *node;
    iot_task_t *task;

    if (delta == 0U) {
        return;
    }

    list_init(&expired);

    iot_port_critical_enter();
    iot_tick_count += delta;

    /* 1) 摘下所有到期任务（此时仍持有临界区，链表不会被并发修改） */
    for (;;) {
        node = list_first(&s_delayed);
        if (node == NULL) {
            break;
        }

        task = iot_delayed_task(node);
        if (IOT_TICK_AFTER(task->wake_tick, iot_tick_count)) {
            break;                  /* 后续任务尚未到期 */
        }

        list_remove(&task->delayed_node);

        if (task->state == IOT_TASK_BLOCKED) {
            /* 带超时阻塞：从 O(1) 等待队列摘除并返回超时结果 */
            if (task->wait_queue != NULL) {
                iot_wait_queue_remove(task->wait_queue, task);
            }
            task->wait_result = IOT_TIMEOUT;
        } else {
            task->wait_result = IOT_OK;
        }

        /* 暂存到局部链表，借用 delayed_node（已摘下，处于游离态） */
        list_add_tail(&expired, &task->delayed_node);
    }

    /* 2) 运行统计：当前任务累计运行 tick */
#if IOT_USE_STATS
    if (iot_current_task != NULL && iot_current_task != iot_idle_task &&
        iot_current_task->state == IOT_TASK_RUNNING) {
        iot_current_task->run_ticks += delta;
    }
#endif

    /* 3) 时间片消耗 */
#if IOT_USE_TIME_SLICING
    iot_sched_tick_slice(delta);
#endif

    iot_port_critical_exit();

    /* 4) 临界区外逐个唤醒，避免长时间关中断 */
    while ((node = list_pop_first(&expired)) != NULL) {
        task = IOT_CONTAINER_OF(node, iot_task_t, delayed_node);
        iot_task_wake(task, task->wait_result);

        LOG_D(IOT_TAG, "timeout/wake: task=%s tick=%u",
                  task->name != NULL ? task->name : "?",
                  (unsigned)iot_tick_count);
    }

    /* 5) 处理软件定时器（回调在调用者上下文执行） */
    iot_timer_tick(iot_tick_count);

    /* 6) 时间片耗尽：请求一次重新调度（MCU 端触发 PendSV） */
#if IOT_USE_TIME_SLICING
    if (iot_sched_slice_expired()) {
        iot_port_request_switch();
    }
#endif
}

void iot_tick(void)
{
    iot_tick_increment(1U);
}

uint32_t iot_tick_get(void)
{
    return iot_tick_count;
}

uint32_t iot_idle_next_timeout(void)
{
    uint32_t now = iot_tick_count;
    uint32_t delayed = iot_delayed_next_deadline();
    uint32_t timer = iot_timer_next_deadline();
    uint32_t next = (delayed < timer) ? delayed : timer;

    if (next == UINT32_MAX) {
        return UINT32_MAX;          /* 没有任何截止点 */
    }
    if (IOT_TICK_AFTER(next, now)) {
        return next - now;
    }
    return 1U;                      /* 已到期：推进 1 tick 触发处理 */
}
