/*
 * ros_sched.c - 就绪表与调度器
 *
 * 数据结构：
 *   - 每个优先级一条环形链表 s_ready[prio]；
 *   - s_ready_bitmap 的 bit i 为 1 表示优先级 i 有就绪任务；
 *   - 取最高优先级就绪任务 = 求 bitmap 最低置位（ctz，优先用硬件指令）。
 *
 * 运行中的任务不在就绪表上：
 *   让出/睡眠/阻塞前由调用方先把任务归位（就绪表或等待/延时链表）。
 *
 * 抢占入口：
 *   - 任务上下文直接调用 ros_sched()；
 *   - 中断/PendSV 上下文调用 ros_sched_from_isr()，只做选择与状态迁移，
 *     实际寄存器保存恢复由移植层的 ros_port_switch() 完成。
 */

#include "ros.h"
#include "common.h"
#include "ros_port.h"

#include <string.h>

/* 调度器拥有的全局状态 */
ros_task_t *ros_current_task = NULL;

/*
 * 切换临界标记（非 0 表示“目标已出就绪表、账本已改，但栈尚未切换”）。
 *
 * ros_sched_pick() 在任务上下文里把目标任务从就绪表摘出后置位，直到
 * 移植层真正完成切栈才清除（Cortex-M 在 SVC handler 内清）。
 * PendSV 看到该标记必须立即让路：否则它会用“已经改成新任务”的
 * ros_current_task 去保存旧任务的 PSP，造成账本与栈永久错位。
 */
volatile uint8_t ros_switch_pending;

static uint32_t s_ready_bitmap;
static list_t s_ready[ROS_MAX_PRIORITY];
static bool s_slice_expired;          /* 时间片耗尽，待切换 */

#if ROS_USE_STATS
static uint32_t s_ctx_switches;       /* 上下文切换总次数 */
#endif

/* ------------------------------ 就绪表 ------------------------------ */

void ros_sched_init(void)
{
    int i;

    for (i = 0; i < ROS_MAX_PRIORITY; i++) {
        list_init(&s_ready[i]);
    }
    s_ready_bitmap = 0U;
    s_slice_expired = false;
    ros_current_task = NULL;

#if ROS_USE_STATS
    s_ctx_switches = 0U;
#endif
}

/* 把任务挂到对应优先级的就绪链表尾部，并置位就绪位图。 */
void ros_make_ready(ros_task_t *task)
{
    uint8_t prio;

    if (task == NULL || task->state == ROS_TASK_TERMINATED ||
        task->state == ROS_TASK_SUSPENDED) {
        return;                  /* 挂起任务不能被唤醒路径意外放回就绪表 */
    }

    prio = task->priority;
    if (prio >= ROS_MAX_PRIORITY) {
        prio = ROS_PRIORITY_IDLE;
    }

    task->state = ROS_TASK_READY;
    list_add_tail(&s_ready[prio], &task->ready_node);
    s_ready_bitmap |= (1U << prio);

    ROS_TRACE_DBG(ROS_EVT_TASK_READY, (uint32_t)(uintptr_t)task, prio);

    LOG_D(ROS_TAG, "ready: task=%s prio=%u",
              task->name != NULL ? task->name : "?", (unsigned)prio);
}

/*
 * 取出最高优先级就绪任务（出表并置 RUNNING）；无就绪任务返回 NULL。
 * 若位图与链表不一致，清除该位并断言暴露问题。
 */
ros_task_t *ros_sched_select(void)
{
    int prio;
    list_node_t *node;
    ros_task_t *task;

    prio = ros_ctz32(s_ready_bitmap);
    if (prio < 0) {
        return NULL;   /* 无就绪任务 */
    }

    node = list_pop_first(&s_ready[prio]);
    if (node == NULL) {
        /* 位图与链表不一致：清除该位自愈 */
        s_ready_bitmap &= ~(1U << prio);
        return NULL;
    }

    if (list_empty(&s_ready[prio])) {
        s_ready_bitmap &= ~(1U << prio);
    }

    task = ROS_CONTAINER_OF(node, ros_task_t, ready_node);
    task->state = ROS_TASK_RUNNING;
    return task;
}

/*
 * 从就绪表摘除任务（动态对象销毁用），状态置 TERMINATED。
 * 任务处于运行/阻塞/延时态时返回 ROS_BUSY。
 */
int ros_sched_remove(ros_task_t *task, bool *was_ready)
{
    int rc = ROS_OK;
    bool ready = false;

    if (task == NULL) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    if (task->state == ROS_TASK_READY) {
        ready = true;
        list_remove(&task->ready_node);
        if (list_empty(&s_ready[task->priority])) {
            s_ready_bitmap &= ~(1U << task->priority);
        }
        task->state = ROS_TASK_TERMINATED;
    } else if (task->state != ROS_TASK_TERMINATED) {
        /* 运行/阻塞/延时中的任务不能直接摘除，交由调用方处理 */
        rc = ROS_BUSY;
    }
    ros_port_critical_exit();

    if (was_ready != NULL) {
        *was_ready = ready;
    }
    return rc;
}

/* ------------------------------ 挂起 / 恢复 ------------------------------ */

/*
 * 把任务从就绪表摘除并置 SUSPENDED。
 * 仅 READY / RUNNING / SUSPENDED 允许；BLOCKED / DELAYED / TERMINATED
 * 返回 ROS_BUSY（避免破坏等待队列与延时链表的节点归属）。
 */
int ros_sched_suspend(ros_task_t *task)
{
    int rc = ROS_BUSY;

    if (task == NULL) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    if (task->state == ROS_TASK_READY) {
        uint8_t prio = task->priority;

        list_remove(&task->ready_node);
        if ((prio < ROS_MAX_PRIORITY) && list_empty(&s_ready[prio])) {
            s_ready_bitmap &= ~(1U << prio);
        }
        task->state = ROS_TASK_SUSPENDED;
        rc = ROS_OK;
    } else if ((task->state == ROS_TASK_RUNNING) ||
               (task->state == ROS_TASK_SUSPENDED)) {
        /* RUNNING 任务本来就不在就绪表上，只需改状态 */
        task->state = ROS_TASK_SUSPENDED;
        rc = ROS_OK;
    }
    ros_port_critical_exit();

    return rc;
}

/* 把 SUSPENDED 任务放回就绪表；非挂起态为空操作 */
int ros_sched_resume(ros_task_t *task)
{
    if (task == NULL) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    if (task->state == ROS_TASK_SUSPENDED) {
        /* ros_make_ready() 会拒绝 SUSPENDED 态，必须先切到 READY 再挂链 */
        task->state = ROS_TASK_READY;
        ros_make_ready(task);
    }
    ros_port_critical_exit();

    return ROS_OK;
}

/* ------------------------------ 优先级变更 ------------------------------ */

void ros_sched_change_priority_locked(ros_task_t *task, uint8_t new_prio)
{
    uint8_t old_prio;

    if (task == NULL || new_prio >= ROS_MAX_PRIORITY) {
        return;
    }

    old_prio = task->priority;
    if (old_prio == new_prio) {
        return;
    }

    if (task->state == ROS_TASK_READY) {
        /* 在就绪表上：换链并维护位图 */
        list_remove(&task->ready_node);
        if (list_empty(&s_ready[old_prio])) {
            s_ready_bitmap &= ~(1U << old_prio);
        }
        task->priority = new_prio;
        list_add_tail(&s_ready[new_prio], &task->ready_node);
        s_ready_bitmap |= (1U << new_prio);
    } else {
        /* 运行/阻塞/延时：只改字段，回到就绪表时按新优先级挂链 */
        task->priority = new_prio;
    }

    ROS_TRACE(ROS_EVT_PRIO_CHANGE, (uint32_t)(uintptr_t)task,
              ((uint32_t)old_prio << 8) | (uint32_t)new_prio);
}

/* 带临界区保护的优先级调整（外部接口）。 */
void ros_sched_change_priority(ros_task_t *task, uint8_t new_prio)
{
    ros_port_critical_enter();
    ros_sched_change_priority_locked(task, new_prio);
    ros_port_critical_exit();
}

/* ------------------------------ 调度 ------------------------------ */

ros_task_t *ros_sched_pick(void)
{
    ros_task_t *next = ros_sched_select();

    next = (next != NULL) ? next : ros_idle_task;

    /*
     * 任务上下文：目标任务已从就绪表摘出，从此刻起直到真正切栈
     * 不允许 PendSV 介入（它会把旧任务的 PSP 记到新任务账上）。
     * 中断上下文走 ros_sched_from_isr() → ros_sched_pick()，
     * 此时无需该标记（PendSV 自身已在临界状态）。
     */
    if (!ros_port_in_isr()) {
        ros_switch_pending = 1U;
    }

    return next;
}

#if ROS_USE_STACK_CHECK
/* 校验任务栈底魔数是否被破坏 */
static void ros_stack_guard(const ros_task_t *task)
{
    const uint32_t *base;

    if (task == NULL || task->stack_mem == NULL ||
        task->stack_size < sizeof(uint32_t)) {
        return;
    }

    base = (const uint32_t *)task->stack_mem;
    if (*base != ROS_STACK_MAGIC) {
        ROS_TRACE(ROS_EVT_STACK_OVERFLOW, (uint32_t)(uintptr_t)task,
                  task->stack_size);
        LOG_E(ROS_TAG, "stack overflow detected: task=%s size=%u",
                  task->name != NULL ? task->name : "?", (unsigned)task->stack_size);
    }
}
#endif

#if defined(ROS_PLATFORM_STM32)
/*
 * 临时诊断：SRAM 顶部的跨复位轨迹环（NVIC_SystemReset 不清 SRAM）。
 * 记录最后 12 次切换的 (prev, next, psp, next->stack_ptr) 与合法性标志，
 * 崩溃复位后由 ros_diag_dump() 在下次启动时打印，用于定位
 * 「ros_current_task 与真实运行栈错位」。
 * flag bit0=1: 当前 PSP 不在 prev 的栈内；bit1=2: next->stack_ptr 非法。
 */
#define ROS_DIAG_BASE   ((volatile uint32_t *)0x2001F000U)
#define ROS_DIAG_MAGIC  0x52444731U
#define ROS_DIAG_SLOTS  12U
#define ROS_DIAG_STRIDE 6U

static void ros_diag_record(uint32_t tag, const ros_task_t *prev,
                            const ros_task_t *next)
{
    volatile uint32_t *b = ROS_DIAG_BASE;
    uint32_t psp;
    uint32_t flags = 0U;
    uint32_t slot;
    uintptr_t lo;

    __asm volatile ("mrs %0, psp" : "=r" (psp));

    if (b[0] != ROS_DIAG_MAGIC) {
        b[0] = ROS_DIAG_MAGIC;
        b[1] = 0U;
    }

    if ((prev != NULL) && (prev->stack_mem != NULL) &&
        (prev->stack_size != 0U)) {
        lo = (uintptr_t)prev->stack_mem;
        if (((uintptr_t)psp < lo) || ((uintptr_t)psp >= lo + prev->stack_size)) {
            flags |= 1U;
        }
    }
    if ((next != NULL) && (next->stack_mem != NULL) &&
        (next->stack_size != 0U)) {
        lo = (uintptr_t)next->stack_mem;
        if (((uintptr_t)next->stack_ptr < lo) ||
            ((uintptr_t)next->stack_ptr >= lo + next->stack_size)) {
            flags |= 2U;
        }
    }

    slot = 2U + ((b[1] % ROS_DIAG_SLOTS) * ROS_DIAG_STRIDE);
    b[slot + 0U] = tag;
    b[slot + 1U] = (uint32_t)(uintptr_t)prev;
    b[slot + 2U] = (uint32_t)(uintptr_t)next;
    b[slot + 3U] = psp;
    b[slot + 4U] = (next != NULL) ? (uint32_t)(uintptr_t)next->stack_ptr : 0U;
    b[slot + 5U] = flags;
    b[1] = b[1] + 1U;
}

/* 打印上次崩溃前的切换轨迹（仅启动早期调用一次） */
void ros_diag_dump(void)
{
    volatile uint32_t *b = ROS_DIAG_BASE;
    uint32_t total;
    uint32_t n;
    uint32_t first;
    uint32_t i;

    if (b[0] != ROS_DIAG_MAGIC) {
        return;
    }

    total = b[1];
    n = (total < ROS_DIAG_SLOTS) ? total : ROS_DIAG_SLOTS;
    first = total - n;
    LOG_I(ROS_TAG, "diag ring: total=%u", (unsigned)total);
    for (i = 0U; i < n; i++) {
        uint32_t slot = 2U + (((first + i) % ROS_DIAG_SLOTS) * ROS_DIAG_STRIDE);

        LOG_I(ROS_TAG, "  #%u tag=%u prev=%08x next=%08x psp=%08x nsp=%08x f=%u",
              (unsigned)(first + i), (unsigned)b[slot + 0U],
              (unsigned)b[slot + 1U], (unsigned)b[slot + 2U],
              (unsigned)b[slot + 3U], (unsigned)b[slot + 4U],
              (unsigned)b[slot + 5U]);
    }
    b[0] = 0U;
}
#else
/* 非 STM32 平台（PC 模拟）没有 PSP/固定 SRAM 诊断环：空实现 */
static void ros_diag_record(uint32_t tag, const ros_task_t *prev,
                            const ros_task_t *next)
{
    (void)tag;
    (void)prev;
    (void)next;
}

void ros_diag_dump(void)
{
}
#endif

/*
 * 执行实际上下文切换：
 * 切走前校验栈底魔数，重置目标任务时间片并累计切换统计，
 * 最后交给移植层 ros_port_switch() 保存/恢复寄存器。
 */
void ros_sched_switch(ros_task_t *next)
{
    ros_task_t *prev = ros_current_task;

    if (next == NULL || next == prev) {
        ros_switch_pending = 0U;    /* 无需切换：撤销 pick 置的标记 */
        return;
    }

#if ROS_USE_STACK_CHECK
    ros_stack_guard(prev);
#endif
    ros_diag_record(1U, prev, next);

#if ROS_USE_TIME_SLICING
    next->slice_remaining = ROS_TIME_SLICE_TICKS;
#endif

    ros_current_task = next;
    next->state = ROS_TASK_RUNNING;

#if ROS_USE_STATS
    s_ctx_switches++;
    next->switches++;
#endif

    ROS_TRACE(ROS_EVT_SWITCH, (uint32_t)(uintptr_t)next,
              (uint32_t)(uintptr_t)prev);

    LOG_D(ROS_TAG, "switch: %s -> %s",
              (prev != NULL && prev->name != NULL) ? prev->name : "(none)",
              next->name != NULL ? next->name : "?");

    ros_port_switch(prev, next);

    /*
     * 协作式端口在切换回来后才会执行到这里：撤销标记。
     * Cortex-M 在 SVC handler 内已完成切换并清除，这里是幂等兜底。
     */
    ros_switch_pending = 0U;
}

/* 任务上下文主动调度一次：选出下一个任务并在临界区外切换。 */
void ros_sched(void)
{
    ros_task_t *next;

    ros_port_critical_enter();
    next = ros_sched_pick();
    ros_port_critical_exit();

    ros_sched_switch(next);
}

/*
 * 中断上下文调度：只做状态迁移，返回需要切入的任务（NULL 表示不用切）。
 * 由移植层的 PendSV 处理程序调用，随后用它完成寄存器切换。
 *
 * 调用者（PendSV 汇编）已全程屏蔽中断，本函数不得再进出临界区：
 * 那会在异常中途执行 cpsie i，使 PendSV 被嵌套、PSP 处于半成品状态。
 */
ros_task_t *ros_sched_from_isr(void)
{
    ros_task_t *prev = ros_current_task;
    ros_task_t *next;

#if ROS_USE_STACK_CHECK
    ros_stack_guard(prev);      /* 抢占路径同样校验栈底魔数 */
#endif

    /*
     * 被抢占的 RUNNING 任务必须回到就绪表：
     * 否则高优先级任务唤醒触发抢占时，prev 会停在 RUNNING 状态
     * 却既不在就绪表也不再运行，被永久“丢掉”。
     * 时间片到期同样走这里（放回同优先级队尾实现轮转）。
     */
    s_slice_expired = false;
    if (prev != NULL && prev != ros_idle_task &&
        prev->state == ROS_TASK_RUNNING) {
        ros_make_ready(prev);
    }

    next = ros_sched_pick();
    ros_diag_record(2U, prev, next);

    if (next != prev) {
        ros_current_task = next;
        next->state = ROS_TASK_RUNNING;

#if ROS_USE_STATS
        s_ctx_switches++;
        next->switches++;
#endif

        ROS_TRACE(ROS_EVT_SWITCH, (uint32_t)(uintptr_t)next,
                  (uint32_t)(uintptr_t)prev);
        return next;
    }

    return NULL;
}

/* 判断就绪表中是否存在比当前任务优先级更高的任务（FromISR 抢占依据）。 */
bool ros_sched_preempt_needed(void)
{
    ros_task_t *cur = ros_current_task;
    int prio;

    if (cur == NULL) {
        return false;
    }

    /* 就绪位图最低置位即最高就绪优先级；数值更小表示应抢占 */
    prio = ros_ctz32(s_ready_bitmap);
    return (prio >= 0) && ((uint8_t)prio < cur->priority);
}

#if ROS_USE_TIME_SLICING
/* 按 delta 消耗当前任务时间片，耗尽时置位切换标志供中断退出时调度。 */
void ros_sched_tick_slice(uint32_t delta)
{
    ros_task_t *cur = ros_current_task;

    /* 空闲任务不参与轮转；只有运行态任务消耗时间片 */
    if (cur == NULL || cur == ros_idle_task || cur->state != ROS_TASK_RUNNING) {
        return;
    }

    if (cur->slice_remaining > delta) {
        cur->slice_remaining -= delta;
    } else {
        cur->slice_remaining = 0U;
    }
    if (cur->slice_remaining == 0U) {
        s_slice_expired = true;
    }
}
#endif

/* 查询当前时间片是否已耗尽。 */
bool ros_sched_slice_expired(void)
{
    return s_slice_expired;
}

#if ROS_USE_STATS
/* 返回上下文切换总次数（ROS_USE_STATS 开启时有效）。 */
uint32_t ros_sched_switch_count(void)
{
    return s_ctx_switches;
}

/* 清零上下文切换计数。 */
void ros_sched_reset_switch_count(void)
{
    s_ctx_switches = 0U;
}
#endif

/* 返回当前运行任务指针（尚未启动时为 NULL）。 */
ros_task_t *ros_current(void)
{
    return ros_current_task;
}
