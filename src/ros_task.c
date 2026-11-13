/*
 * ros_task.c - 任务管理：创建、让出、睡眠、退出、通知、注册表
 *
 * 任务生命周期：
 *   ros_task_create() 创建后进入就绪表并挂入任务注册表；
 *   被调度器选中时运行，直到 yield / sleep / 阻塞 / 退出；
 *   任务入口返回后由移植层调用 ros_task_exit() 标记终止并移出注册表。
 *
 * 栈溢出检测与水位线：
 *   创建时把整个任务栈按 32 位字填满 ROS_STACK_MAGIC，随后由移植层
 *   从栈顶写入初始帧。栈向下增长，因此从栈底向上数「连续保持魔数」
 *   的字数即为从未使用过的栈空间，ros_task_get_stats() 可直接读出水位。
 *   每次任务切换前额外校验栈底第一个字，作为越界的最后一道防线。
 */

#include "ros.h"
#include "common.h"
#include "ros_port.h"
#include "ros_assert.h"
#include "ros_mem.h"

#include <string.h>

/* 任务注册表：供 ros_stats_reset() 遍历所有任务清零运行统计 */
static list_t s_all_tasks;
/* 任务 ID 分配器：单调递增，永不复用（ps/kill 按 #tid 定位） */
static uint16_t s_next_tid;

#if ROS_USE_NOTIFY
/* 所有等待通知的任务共用一条 O(1) 等待队列 */
static ros_wait_queue_t s_notify_wait;
#endif

/* 初始化任务注册表与通知等待队列（由 ros_init 调用）。 */
void ros_task_module_init(void)
{
    list_init(&s_all_tasks);
    s_next_tid = 0U;
#if ROS_USE_NOTIFY
    ros_notify_init();
#endif
}

/* 把任务 TCB 挂入全局注册表，供统计与遍历使用。 */
void ros_task_register(ros_task_t *task)
{
    if (task == NULL) {
        return;
    }

    list_node_init(&task->all_node);
    ros_port_critical_enter();
    task->tid = ++s_next_tid;
    if (task->tid == 0U) {
        task->tid = ++s_next_tid;       /* 回绕时跳过 0 */
    }
    list_add_tail(&s_all_tasks, &task->all_node);
    ros_port_critical_exit();
}

/* 返回注册表首个任务；空表或未初始化返回 NULL。 */
ros_task_t *ros_task_first(void)
{
    list_node_t *node = s_all_tasks.head.next;

    if (node == NULL || node == &s_all_tasks.head) {
        return NULL;
    }
    return ROS_CONTAINER_OF(node, ros_task_t, all_node);
}

/* 返回 task 之后的注册表节点；task 为 NULL 时从表头开始。 */
ros_task_t *ros_task_next(const ros_task_t *task)
{
    list_node_t *node;

    if (task == NULL) {
        return ros_task_first();
    }
    node = task->all_node.next;
    if (node == &s_all_tasks.head) {
        return NULL;
    }
    return ROS_CONTAINER_OF(node, ros_task_t, all_node);
}

/* 当前注册任务总数（含 idle）。 */
uint32_t ros_task_total(void)
{
    uint32_t n = 0U;
    const ros_task_t *task;

    for (task = ros_task_first(); task != NULL; task = ros_task_next(task)) {
        n++;
    }
    return n;
}

/* 遍历任务注册表，清零每个任务的运行统计。 */
void ros_task_reset_stats_all(void)
{
#if ROS_USE_STATS
    list_node_t *node;

    for (node = s_all_tasks.head.next; node != &s_all_tasks.head;
         node = node->next) {
        ros_task_t *task = ROS_CONTAINER_OF(node, ros_task_t, all_node);

        task->run_ticks = 0U;
        task->switches = 0U;
    }
#endif
}

#if ROS_USE_STACK_CHECK
/* 用魔数填满整个可对齐的任务栈 */
static void ros_stack_fill(void *stack_mem, uint32_t stack_size)
{
    uint32_t *words = (uint32_t *)stack_mem;
    uint32_t total = stack_size / (uint32_t)sizeof(uint32_t);
    uint32_t i;

    for (i = 0U; i < total; i++) {
        words[i] = ROS_STACK_MAGIC;
    }
}
#endif

/*
 * 任务创建公共实现：校验参数、填充 TCB、填充栈魔数、构造移植层上下文，
 * 最后加入就绪表与任务注册表。stack_owned 标识栈是否由动态接口分配。
 */
static int ros_task_create_impl(ros_task_t *task, const char *name,
                                uint8_t priority, ros_entry_t entry,
                                void *arg, void *stack_mem,
                                uint32_t stack_size, bool stack_owned,
                                ros_io_t *stdio)
{
    int rc;

    if (task == NULL || entry == NULL || stack_mem == NULL || stack_size == 0U) {
        LOG_E(ROS_TAG, "task_create: invalid param");
        return ROS_INVALID_PARAM;
    }
    if (priority >= ROS_PRIORITY_IDLE) {
        LOG_E(ROS_TAG, "task_create: priority %u reserved for idle", (unsigned)priority);
        return ROS_INVALID_PARAM;   /* 最低优先级保留给空闲任务 */
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
    /* stdio 未指定时继承创建者（脚本 JOB 因此共享父任务的 stdio） */
    task->stdio = (stdio != NULL) ? stdio : ros_stdio();
    task->state = ROS_TASK_READY;
    task->created = true;
    task->stack_owned = stack_owned;

#if ROS_USE_TIME_SLICING
    task->slice_remaining = ROS_TIME_SLICE_TICKS;
#endif
#if ROS_USE_PRIORITY_INHERIT
    task->wait_mutex = NULL;
#endif

#if ROS_USE_STACK_CHECK
    /* 必须在构造上下文之前填充：初始帧会覆盖栈顶部分魔数 */
    ros_stack_fill(stack_mem, stack_size);
#endif

    /* 链表节点初始化为游离态 */
    list_node_init(&task->ready_node);
    list_node_init(&task->delayed_node);
    list_node_init(&task->wait_node);
    task->wait_queue = NULL;

    /* 由移植层构造任务上下文/栈帧；失败时不得进入就绪表 */
    rc = ros_port_stack_init(task);
    if (rc != ROS_OK) {
        LOG_E(ROS_TAG, "task_create: port stack init failed: %d", rc);
        return rc;
    }

    /* 加入就绪表与任务注册表 */
    ros_port_critical_enter();
    ROS_TRACE(ROS_EVT_TASK_CREATE, (uint32_t)(uintptr_t)task, priority);
    ros_trace_name(task, ROS_TRACE_NAME_TASK, name);

    ros_make_ready(task);
    ros_port_critical_exit();
    ros_task_register(task);

    LOG_I(ROS_TAG, "task create: name=%s prio=%u stack=%u",
              name != NULL ? name : "?", (unsigned)priority,
              (unsigned)stack_size);
    return ROS_OK;
}

/* 静态创建任务：TCB 与任务栈均由调用者提供。 */
int ros_task_create(ros_task_t *task, const char *name,
                    uint8_t priority, ros_entry_t entry, void *arg,
                    void *stack_mem, uint32_t stack_size)
{
    return ros_task_create_impl(task, name, priority, entry, arg,
                                stack_mem, stack_size, false, NULL);
}

/* 静态创建任务：并显式指定 stdio。 */
int ros_task_create_io(ros_task_t *task, const char *name,
                       uint8_t priority, ros_entry_t entry, void *arg,
                       void *stack_mem, uint32_t stack_size, ros_io_t *stdio)
{
    return ros_task_create_impl(task, name, priority, entry, arg,
                                stack_mem, stack_size, false, stdio);
}

/* 动态创建的公共实现：stdio 为 NULL 时继承创建者。 */
static ros_task_t *ros_task_create_dynamic_impl(const char *name,
                                                uint8_t priority,
                                                ros_entry_t entry, void *arg,
                                                uint32_t stack_size,
                                                ros_io_t *stdio)
{
    ros_task_t *task;
    void *stack;

    if (entry == NULL || stack_size == 0U) {
        LOG_E(ROS_TAG, "task_create_dynamic: invalid param");
        return NULL;
    }

    task = (ros_task_t *)ros_mem_calloc(1U, sizeof(*task));
    if (task == NULL) {
        LOG_E(ROS_TAG, "task_create_dynamic: no memory for tcb");
        return NULL;
    }
    stack = ros_mem_malloc(stack_size);
    if (stack == NULL) {
        LOG_E(ROS_TAG, "task_create_dynamic: no memory for stack");
        ros_mem_free(task);
        return NULL;
    }

    if (ros_task_create_impl(task, name, priority, entry, arg,
                             stack, stack_size, true, stdio) != ROS_OK) {
        ros_mem_free(stack);
        ros_mem_free(task);
        return NULL;
    }

    return task;
}

/* 动态创建任务：TCB 与任务栈均由 ros_mem_* 分配，失败返回 NULL。 */
ros_task_t *ros_task_create_dynamic(const char *name, uint8_t priority,
                                    ros_entry_t entry, void *arg,
                                    uint32_t stack_size)
{
    return ros_task_create_dynamic_impl(name, priority, entry, arg,
                                        stack_size, NULL);
}

/* 动态创建任务：并显式指定 stdio。 */
ros_task_t *ros_task_create_dynamic_io(const char *name, uint8_t priority,
                                       ros_entry_t entry, void *arg,
                                       uint32_t stack_size, ros_io_t *stdio)
{
    return ros_task_create_dynamic_impl(name, priority, entry, arg,
                                        stack_size, stdio);
}

ros_io_t *ros_task_get_stdio(const ros_task_t *task)
{
    if (task == NULL) {
        task = ros_current_task;
    }
    if (task != NULL && task->stdio != NULL) {
        return task->stdio;
    }
    return ros_io_default();
}

int ros_task_set_stdio(ros_task_t *task, ros_io_t *stdio)
{
    if (task == NULL) {
        task = ros_current_task;
    }
    if (task == NULL) {
        return ROS_INVALID_PARAM;
    }
    task->stdio = (stdio != NULL) ? stdio : ros_io_default();
    return ROS_OK;
}

/*
 * 销毁动态任务：仅允许销毁「就绪未运行」或「已退出」的任务；
 * 运行/阻塞/延时中的任务返回 ROS_BUSY（需先退出由其他任务回收）。
 * 成功时依次释放端口上下文、任务栈与 TCB。
 */
int ros_task_destroy(ros_task_t *task)
{
    bool was_ready;

    if (task == NULL || task == ros_idle_task || !task->stack_owned) {
        return ROS_INVALID_PARAM;   /* 仅支持动态创建的任务 */
    }
    if (task == ros_current_task) {
        return ROS_BUSY;            /* 不能在自身上下文释放自己的栈 */
    }

    was_ready = (task->state == ROS_TASK_READY);
    if (ros_sched_remove(task) != ROS_OK) {
        return ROS_BUSY;            /* 运行/阻塞/延时中的任务不能销毁 */
    }

    if (was_ready) {
        /* 就绪任务仍在注册表上；已退出任务在 ros_task_exit 中已摘除 */
        ros_port_critical_enter();
        list_remove(&task->all_node);
        ros_port_critical_exit();
    }

    LOG_I(ROS_TAG, "task destroy: name=%s",
              task->name != NULL ? task->name : "?");

    ROS_TRACE(ROS_EVT_TASK_DESTROY, (uint32_t)(uintptr_t)task, 0U);

    ros_port_stack_deinit(task);
    ros_mem_free(task->stack_mem);
    ros_mem_free(task);
    return ROS_OK;
}

/* 让出 CPU：当前任务放回同优先级就绪队尾，触发一次调度。 */
void ros_yield(void)
{
    ros_task_t *cur = ros_current_task;
    ros_task_t *next;

    ros_port_critical_enter();
    if (cur != NULL && cur != ros_idle_task &&
        cur->state != ROS_TASK_TERMINATED) {
        /* 放回同优先级就绪表尾，实现同优先级轮转 */
        ros_make_ready(cur);
    }
    next = ros_sched_pick();
    ros_port_critical_exit();

    ros_sched_switch(next);
}

/* 终止当前任务：标记 TERMINATED、移出注册表并切换到下一个任务（通常不返回）。 */
void ros_task_exit(void)
{
    ros_task_t *cur = ros_current_task;
    ros_task_t *next;

    ros_port_critical_enter();
    if (cur != NULL) {
        cur->state = ROS_TASK_TERMINATED;
        list_remove(&cur->all_node);
    }
    next = ros_sched_pick();
    ros_port_critical_exit();

    LOG_I(ROS_TAG, "task exit: %s",
              (cur != NULL && cur->name != NULL) ? cur->name : "?");

    ROS_TRACE(ROS_EVT_TASK_EXIT, (uint32_t)(uintptr_t)cur, 0U);

    ros_sched_switch(next);    /* 切走，通常不再返回 */

    for (;;) {
        ros_yield();   /* 兜底：万一被重新调度也立即让出 */
    }
}

/* 睡眠指定毫秒（基于系统 tick）。 */
void ros_sleep_ms(uint32_t ms)
{
    ros_sleep_until(ros_tick_get() + ms);
}

/*
 * 睡眠到指定 tick 时刻：挂入延时链表并切换任务，
 * 到期后由 ros_tick_increment() 唤醒。
 */
void ros_sleep_until(uint32_t wake_tick)
{
    ros_task_t *cur = ros_current_task;
    ros_task_t *next;

    if (ros_in_isr()) {
        ROS_ASSERT(!ros_in_isr());
        LOG_W(ROS_TAG, "sleep: forbidden in ISR");
        return;
    }
    if (cur == NULL || cur == ros_idle_task) {
        return;         /* 空闲任务/未启动时不睡眠 */
    }
    if (!ROS_TICK_AFTER(wake_tick, ros_tick_count)) {
        return;         /* 已到期，无需睡眠 */
    }

    ros_port_critical_enter();
    cur->wake_tick = wake_tick;
    cur->state = ROS_TASK_DELAYED;
    ros_delayed_add(cur);
    next = ros_sched_pick();   /* 入表与选择在同一临界区，避免唤醒竞态 */
    ros_port_critical_exit();

    LOG_D(ROS_TAG, "sleep: task=%s until=%u",
              cur->name != NULL ? cur->name : "?", (unsigned)wake_tick);

    ROS_TRACE(ROS_EVT_TASK_SLEEP, (uint32_t)(uintptr_t)cur,
              wake_tick - ros_tick_count);

    ros_sched_switch(next);    /* 切走，tick 到期后由 ros_tick.c 唤醒 */
}

/*
 * 修改任务基准优先级：处于继承提升状态时只更新基准值，
 * 待继承解除后自然回落，避免破坏优先级继承链。
 */
int ros_task_set_priority(ros_task_t *task, uint8_t priority)
{
    uint8_t old_base;

    if (task == NULL || priority >= ROS_PRIORITY_IDLE) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    old_base = task->base_priority;
    task->base_priority = priority;

    /* 仅在未处于继承提升状态时立即生效，否则等继承解除后自然回落 */
    if (task->priority == old_base) {
        ros_sched_change_priority_locked(task, priority);
    }
    ros_port_critical_exit();

    return ROS_OK;
}

/* ============================== 挂起 / 恢复 ============================== */

int ros_task_suspend(ros_task_t *task)
{
    ros_task_t *cur = ros_current_task;
    ros_task_t *next;
    int rc;

    if (task == NULL) {
        return ROS_INVALID_PARAM;
    }

    /* 状态迁移与嵌套计数在同一临界区完成，避免与并发 resume 丢计数 */
    ros_port_critical_enter();
    rc = ros_sched_suspend(task);
    if (rc == ROS_OK && task->suspend_count < 0xFFU) {
        task->suspend_count++;
    }
    ros_port_critical_exit();
    if (rc != ROS_OK) {
        return rc;
    }

    ROS_TRACE(ROS_EVT_TASK_SUSPEND, (uint32_t)(uintptr_t)task,
              task->suspend_count);

    /* 挂起自己：当前任务已不在就绪表，直接选下一个任务切走 */
    if (task == cur) {
        ros_port_critical_enter();
        next = ros_sched_pick();
        ros_port_critical_exit();
        ros_sched_switch(next);
    }

    return ROS_OK;
}

int ros_task_resume(ros_task_t *task)
{
    bool resume_now;

    if (task == NULL) {
        return ROS_INVALID_PARAM;
    }

    /* 计数递减与状态判断在同一临界区，ISR 与任务并发 resume 不会丢计数 */
    ros_port_critical_enter();
    if ((task->suspend_count == 0U) ||
        (task->state != ROS_TASK_SUSPENDED)) {
        ros_port_critical_exit();
        return ROS_NOT_READY;
    }
    task->suspend_count--;
    resume_now = (task->suspend_count == 0U);
    ros_port_critical_exit();

    if (!resume_now) {
        return ROS_OK;               /* 仍有嵌套挂起，保持挂起态 */
    }

    (void)ros_sched_resume(task);
    ROS_TRACE(ROS_EVT_TASK_RESUME, (uint32_t)(uintptr_t)task, 0U);

    /* 恢复了更高优先级任务：ISR 中请求 PendSV，任务上下文主动让出 */
    if (ros_in_isr()) {
        ros_port_request_switch();
    } else if (ros_sched_preempt_needed()) {
        ros_yield();
    }

    return ROS_OK;
}

bool ros_task_is_suspended(const ros_task_t *task)
{
    return (task != NULL) && (task->state == ROS_TASK_SUSPENDED);
}

/* ============================== 任务通知 ============================== */

#if ROS_USE_NOTIFY

/* 初始化通知等待队列（所有等待通知的任务共用）。 */
void ros_notify_init(void)
{
    ros_wait_queue_init(&s_notify_wait);
}

/*
 * 向指定任务发送通知：保存通知值并置 pending；
 * 若该任务正阻塞在通知队列上则立即唤醒。
 */
int ros_task_notify(ros_task_t *task, uint32_t value)
{
    if (task == NULL) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    task->notify_value = value;
    task->notify_pending = true;

    /* O(1) 判断该任务是否正阻塞在通知队列上 */
    if (ros_wait_queue_contains(&s_notify_wait, task)) {
        ros_task_wake(task, ROS_OK);
    }
    ros_port_critical_exit();

    ROS_TRACE_OBJ(ROS_EVT_NOTIFY, (uint32_t)(uintptr_t)task, value);
    return ROS_OK;
}

/*
 * 等待本任务通知：已有 pending 立即取走；
 * 否则按 timeout 阻塞在通知队列上，唤醒后返回通知值。
 */
int ros_task_wait_notify(ros_task_t *task, uint32_t timeout, uint32_t *value)
{
    int rc;

    if (task == NULL) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    if (task->notify_pending) {
        task->notify_pending = false;
        if (value != NULL) {
            *value = task->notify_value;
        }
        ros_port_critical_exit();
        return ROS_OK;
    }
    ros_port_critical_exit();

    if (timeout == ROS_TIMEOUT_NONE) {
        return ROS_TIMEOUT;
    }

    rc = ros_block_current(&s_notify_wait, timeout);
    if (rc != ROS_OK) {
        return rc;
    }

    ros_port_critical_enter();
    task->notify_pending = false;
    if (value != NULL) {
        *value = task->notify_value;
    }
    ros_port_critical_exit();

    return ROS_OK;
}

/* 清除未消费的通知（丢弃通知值）。 */
void ros_task_notify_clear(ros_task_t *task)
{
    if (task == NULL) {
        return;
    }

    ros_port_critical_enter();
    task->notify_pending = false;
    ros_port_critical_exit();
}

#endif /* ROS_USE_NOTIFY */
