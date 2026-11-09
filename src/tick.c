/*
 * tick.c - 内核 tick 与延时链表
 *
 * s_delayed 为按 wake_tick 升序排列的延时链表：
 *   - 睡眠任务（IOT_TASK_DELAYED）；
 *   - 带超时阻塞的任务（IOT_TASK_BLOCKED，同时还在对象等待链表上）。
 *
 * iot_tick_increment() 推进时间后，把到期任务逐个移出延时链表：
 *   - 睡眠任务直接转就绪；
 *   - 阻塞任务摘除等待节点、结果置 IOT_TIMEOUT 后转就绪。
 */

#include "iot.h"
#include "port.h"

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
    if (task == NULL) {
        return;
    }

    /* 调用者需已进入临界区（阻塞/睡眠路径均在同一临界区内调用） */
    task->delayed_node.sort_key = task->wake_tick;
    list_insert_sorted(&s_delayed, &task->delayed_node);
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
    iot_port_critical_enter();
    iot_tick_count += delta;

    /* 依次唤醒所有到期任务（延时链表按时间升序） */
    for (;;) {
        list_node_t *node = list_first(&s_delayed);
        iot_task_t *task;

        if (node == NULL) {
            break;
        }
        task = iot_delayed_task(node);
        if (IOT_TICK_AFTER(task->wake_tick, iot_tick_count)) {
            break;                  /* 后续任务尚未到期 */
        }

        list_remove(&task->delayed_node);
        if (task->state == IOT_TASK_BLOCKED) {
            /* 带超时阻塞：摘除等待节点并返回超时结果 */
            list_remove(&task->wait_node);
            task->wait_result = IOT_TIMEOUT;
        }
        iot_make_ready(task);

        LOG_D(IOT_TAG, "timeout/wake: task=%s tick=%u",
                  task->name != NULL ? task->name : "?",
                  (unsigned)iot_tick_count);
    }
    iot_port_critical_exit();

    /* 处理软件定时器（回调在调用者上下文执行） */
    iot_timer_tick(iot_tick_count);
}

void iot_tick(void)
{
    iot_tick_increment(1U);
}

uint32_t iot_tick_get(void)
{
    return iot_tick_count;
}