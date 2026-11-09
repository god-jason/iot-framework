/*
 * iot_task.c - 任务管理：创建、让出、睡眠、退出、通知、注册表
 *
 * 任务生命周期：
 *   iot_task_create() 创建后进入就绪表并挂入任务注册表；
 *   被调度器选中时运行，直到 yield / sleep / 阻塞 / 退出；
 *   任务入口返回后由移植层调用 iot_task_exit() 标记终止并移出注册表。
 *
 * 栈溢出检测与水位线：
 *   创建时把整个任务栈按 32 位字填满 IOT_STACK_MAGIC，随后由移植层
 *   从栈顶写入初始帧。栈向下增长，因此从栈底向上数「连续保持魔数」
 *   的字数即为从未使用过的栈空间，iot_task_get_stats() 可直接读出水位。
 *   每次任务切换前额外校验栈底第一个字，作为越界的最后一道防线。
 */

#include "iot.h"
#include "iot_port.h"
#include "iot_assert.h"

#include <string.h>

/* 任务注册表：供 iot_stats_reset() 遍历所有任务清零运行统计 */
static list_t s_all_tasks;

#if IOT_USE_NOTIFY
/* 所有等待通知的任务共用一条 O(1) 等待队列 */
static iot_wait_queue_t s_notify_wait;
#endif

void iot_task_module_init(void)
{
    list_init(&s_all_tasks);
#if IOT_USE_NOTIFY
    iot_notify_init();
#endif
}

void iot_task_register(iot_task_t *task)
{
    if (task == NULL) {
        return;
    }

    list_node_init(&task->all_node);
    iot_port_critical_enter();
    list_add_tail(&s_all_tasks, &task->all_node);
    iot_port_critical_exit();
}

void iot_task_reset_stats_all(void)
{
#if IOT_USE_STATS
    list_node_t *node;

    for (node = s_all_tasks.head.next; node != &s_all_tasks.head;
         node = node->next) {
        iot_task_t *task = IOT_CONTAINER_OF(node, iot_task_t, all_node);

        task->run_ticks = 0U;
        task->switches = 0U;
    }
#endif
}

#if IOT_USE_STACK_CHECK
/* 用魔数填满整个可对齐的任务栈 */
static void iot_stack_fill(void *stack_mem, uint32_t stack_size)
{
    uint32_t *words = (uint32_t *)stack_mem;
    uint32_t total = stack_size / (uint32_t)sizeof(uint32_t);
    uint32_t i;

    for (i = 0U; i < total; i++) {
        words[i] = IOT_STACK_MAGIC;
    }
}
#endif

int iot_task_create(iot_task_t *task, const char *name,
                    uint8_t priority, iot_entry_t entry, void *arg,
                    void *stack_mem, uint32_t stack_size)
{
    int rc;

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
    task->base_priority = priority;
    task->entry = entry;
    task->arg = arg;
    task->stack_mem = stack_mem;
    task->stack_size = stack_size;
    task->state = IOT_TASK_READY;
    task->created = true;

#if IOT_USE_TIME_SLICING
    task->slice_remaining = IOT_TIME_SLICE_TICKS;
#endif
#if IOT_USE_PRIORITY_INHERIT
    task->wait_mutex = NULL;
#endif

#if IOT_USE_STACK_CHECK
    /* 必须在构造上下文之前填充：初始帧会覆盖栈顶部分魔数 */
    iot_stack_fill(stack_mem, stack_size);
#endif

    /* 链表节点初始化为游离态 */
    list_node_init(&task->ready_node);
    list_node_init(&task->delayed_node);
    list_node_init(&task->wait_node);
    task->wait_queue = NULL;

    /* 由移植层构造任务上下文/栈帧；失败时不得进入就绪表 */
    rc = iot_port_stack_init(task);
    if (rc != IOT_OK) {
        LOG_E(IOT_TAG, "task_create: port stack init failed: %d", rc);
        return rc;
    }

    /* 加入就绪表与任务注册表 */
    iot_port_critical_enter();
    iot_make_ready(task);
    iot_port_critical_exit();
    iot_task_register(task);

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
        list_remove(&cur->all_node);
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

    if (iot_in_isr()) {
        IOT_ASSERT(!iot_in_isr());
        LOG_W(IOT_TAG, "sleep: forbidden in ISR");
        return;
    }
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

    iot_sched_switch(next);    /* 切走，tick 到期后由 iot_tick.c 唤醒 */
}

int iot_task_set_priority(iot_task_t *task, uint8_t priority)
{
    uint8_t old_base;

    if (task == NULL || priority >= IOT_PRIORITY_IDLE) {
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    old_base = task->base_priority;
    task->base_priority = priority;

    /* 仅在未处于继承提升状态时立即生效，否则等继承解除后自然回落 */
    if (task->priority == old_base) {
        iot_sched_change_priority_locked(task, priority);
    }
    iot_port_critical_exit();

    return IOT_OK;
}

/* ============================== 任务通知 ============================== */

#if IOT_USE_NOTIFY

void iot_notify_init(void)
{
    iot_wait_queue_init(&s_notify_wait);
}

int iot_task_notify(iot_task_t *task, uint32_t value)
{
    if (task == NULL) {
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    task->notify_value = value;
    task->notify_pending = true;

    /* O(1) 判断该任务是否正阻塞在通知队列上 */
    if (iot_wait_queue_contains(&s_notify_wait, task)) {
        iot_task_wake(task, IOT_OK);
    }
    iot_port_critical_exit();

    return IOT_OK;
}

int iot_task_wait_notify(iot_task_t *task, uint32_t timeout, uint32_t *value)
{
    int rc;

    if (task == NULL) {
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    if (task->notify_pending) {
        task->notify_pending = false;
        if (value != NULL) {
            *value = task->notify_value;
        }
        iot_port_critical_exit();
        return IOT_OK;
    }
    iot_port_critical_exit();

    if (timeout == IOT_TIMEOUT_NONE) {
        return IOT_TIMEOUT;
    }

    rc = iot_block_current(&s_notify_wait, timeout);
    if (rc != IOT_OK) {
        return rc;
    }

    iot_port_critical_enter();
    task->notify_pending = false;
    if (value != NULL) {
        *value = task->notify_value;
    }
    iot_port_critical_exit();

    return IOT_OK;
}

void iot_task_notify_clear(iot_task_t *task)
{
    if (task == NULL) {
        return;
    }

    iot_port_critical_enter();
    task->notify_pending = false;
    iot_port_critical_exit();
}

#endif /* IOT_USE_NOTIFY */
