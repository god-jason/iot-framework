/*
 * task.c - 任务管理：创建、让出、睡眠、退出
 *
 * 任务生命周期：
 *   iot_task_create() 创建后进入就绪表；
 *   被调度器选中时运行，直到 yield / sleep / 阻塞 / 退出；
 *   任务入口返回后由移植层调用 iot_task_exit() 标记终止。
 */

#include "iot.h"
#include "port.h"

#include <string.h>

iot_status_t iot_task_create(iot_task_t *task, const char *name,
                               uint8_t priority, iot_entry_t entry, void *arg,
                               void *stack_mem, uint32_t stack_size)
{
    if (task == NULL || entry == NULL || stack_mem == NULL || stack_size == 0U) {
        LOG_E(IOT_TAG, "task_create: invalid param");
        return IOT_INVALID_PARAM;
    }
    if (priority >= IOT_PRIORITY_IDLE) {
        LOG_E(IOT_TAG, "task_create: priority %u reserved for idle", (unsigned)priority);
        return IOT_INVALID_PARAM;   /* 最低优先级保留给空闲任务 */
    }

    /* 清空并填充控制块 */
    memset(task, 0, sizeof(*task));
    task->name = name;
    task->priority = priority;
    task->entry = entry;
    task->arg = arg;
    task->stack_mem = stack_mem;
    task->stack_size = stack_size;
    task->state = IOT_TASK_READY;
    task->created = true;

    /* 链表节点初始化为游离态 */
    list_node_init(&task->ready_node);
    list_node_init(&task->delayed_node);
    list_node_init(&task->wait_node);

    /* 由移植层构造任务上下文/栈帧 */
    iot_port_stack_init(task);

    /* 加入就绪表 */
    iot_port_critical_enter();
    iot_make_ready(task);
    iot_port_critical_exit();

    LOG_I(IOT_TAG, "task create: name=%s prio=%u stack=%u",
              name != NULL ? name : "?", (unsigned)priority,
              (unsigned)stack_size);
    return IOT_OK;
}

void iot_yield(void)
{
    iot_task_t *cur = iot_current_task;
    iot_task_t *next;

    iot_port_critical_enter();
    if (cur != NULL && cur != iot_idle_task &&
        cur->state != IOT_TASK_TERMINATED) {
        /* 放回同优先级就绪表尾，实现同优先级轮转 */
        iot_make_ready(cur);
    }
    next = iot_sched_pick();
    iot_port_critical_exit();

    iot_sched_switch(next);
}

void iot_task_exit(void)
{
    iot_task_t *cur = iot_current_task;
    iot_task_t *next;

    iot_port_critical_enter();
    if (cur != NULL) {
        cur->state = IOT_TASK_TERMINATED;
    }
    next = iot_sched_pick();
    iot_port_critical_exit();

    LOG_I(IOT_TAG, "task exit: %s",
              (cur != NULL && cur->name != NULL) ? cur->name : "?");

    iot_sched_switch(next);    /* 切走，通常不再返回 */

    for (;;) {
        iot_yield();   /* 兜底：万一被重新调度也立即让出 */
    }
}

void iot_sleep_ms(uint32_t ms)
{
    iot_sleep_until(iot_tick_get() + ms);
}

void iot_sleep_until(uint32_t wake_tick)
{
    iot_task_t *cur = iot_current_task;
    iot_task_t *next;

    if (cur == NULL || cur == iot_idle_task) {
        return;         /* 空闲任务/未启动时不睡眠 */
    }
    if (!IOT_TICK_AFTER(wake_tick, iot_tick_count)) {
        return;         /* 已到期，无需睡眠 */
    }

    iot_port_critical_enter();
    cur->wake_tick = wake_tick;
    cur->state = IOT_TASK_DELAYED;
    iot_delayed_add(cur);
    next = iot_sched_pick();   /* 入表与选择在同一临界区，避免唤醒竞态 */
    iot_port_critical_exit();

    LOG_D(IOT_TAG, "sleep: task=%s until=%u",
              cur->name != NULL ? cur->name : "?", (unsigned)wake_tick);

    iot_sched_switch(next);    /* 切走，tick 到期后由 tick.c 唤醒 */
}