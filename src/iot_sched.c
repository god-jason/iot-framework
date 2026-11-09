/*
 * iot_sched.c - 就绪表与调度器
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
 *   - 任务上下文直接调用 iot_sched()；
 *   - 中断/PendSV 上下文调用 iot_sched_from_isr()，只做选择与状态迁移，
 *     实际寄存器保存恢复由移植层的 iot_port_switch() 完成。
 */

#include "iot.h"
#include "iot_port.h"
#include "iot_assert.h"

#include <string.h>

/* 调度器拥有的全局状态 */
iot_task_t *iot_current_task = NULL;

static uint32_t s_ready_bitmap;
static list_t s_ready[IOT_MAX_PRIORITY];
static bool s_slice_expired;          /* 时间片耗尽，待切换 */

#if IOT_USE_STATS
static uint32_t s_ctx_switches;       /* 上下文切换总次数 */
#endif

/* ------------------------------ 内部工具 ------------------------------ */

/*
 * 求 32 位整数最低置位的位置（0..31），返回 -1 表示 v 为 0。
 * 优先级 0 最高，因此结果直接就是最高优先级编号。
 */
static int iot_ctz32(uint32_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    if (v == 0U) {
        return -1;
    }
    return __builtin_ctz(v);
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

/* ------------------------------ 就绪表 ------------------------------ */

void iot_sched_init(void)
{
    int i;

    for (i = 0; i < IOT_MAX_PRIORITY; i++) {
        list_init(&s_ready[i]);
    }
    s_ready_bitmap = 0U;
    s_slice_expired = false;
    iot_current_task = NULL;

#if IOT_USE_STATS
    s_ctx_switches = 0U;
#endif
}

void iot_make_ready(iot_task_t *task)
{
    uint8_t prio;

    if (task == NULL || task->state == IOT_TASK_TERMINATED) {
        return;
    }

    prio = task->priority;
    if (prio >= IOT_MAX_PRIORITY) {
        IOT_ASSERT(prio < IOT_MAX_PRIORITY);
        prio = IOT_PRIORITY_IDLE;
    }

    task->state = IOT_TASK_READY;
    list_add_tail(&s_ready[prio], &task->ready_node);
    s_ready_bitmap |= (1U << prio);

    LOG_D(IOT_TAG, "ready: task=%s prio=%u",
              task->name != NULL ? task->name : "?", (unsigned)prio);
}

iot_task_t *iot_sched_select(void)
{
    int prio;
    list_node_t *node;
    iot_task_t *task;

    prio = iot_ctz32(s_ready_bitmap);
    if (prio < 0) {
        return NULL;   /* 无就绪任务 */
    }

    node = list_pop_first(&s_ready[prio]);
    if (node == NULL) {
        /* 位图与链表不一致：清除该位自愈，并断言暴露问题 */
        s_ready_bitmap &= ~(1U << prio);
        IOT_ASSERT(0);
        return NULL;
    }

    if (list_empty(&s_ready[prio])) {
        s_ready_bitmap &= ~(1U << prio);
    }

    task = IOT_CONTAINER_OF(node, iot_task_t, ready_node);
    task->state = IOT_TASK_RUNNING;
    return task;
}

/* ------------------------------ 优先级变更 ------------------------------ */

void iot_sched_change_priority_locked(iot_task_t *task, uint8_t new_prio)
{
    uint8_t old_prio;

    if (task == NULL || new_prio >= IOT_MAX_PRIORITY) {
        return;
    }

    old_prio = task->priority;
    if (old_prio == new_prio) {
        return;
    }

    if (task->state == IOT_TASK_READY) {
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
}

void iot_sched_change_priority(iot_task_t *task, uint8_t new_prio)
{
    iot_port_critical_enter();
    iot_sched_change_priority_locked(task, new_prio);
    iot_port_critical_exit();
}

/* ------------------------------ 调度 ------------------------------ */

iot_task_t *iot_sched_pick(void)
{
    iot_task_t *next = iot_sched_select();

    return (next != NULL) ? next : iot_idle_task;
}

#if IOT_USE_STACK_CHECK
/* 校验任务栈底魔数是否被破坏 */
static void iot_stack_guard(const iot_task_t *task)
{
    const uint32_t *base;

    if (task == NULL || task->stack_mem == NULL ||
        task->stack_size < sizeof(uint32_t)) {
        return;
    }

    base = (const uint32_t *)task->stack_mem;
    if (*base != IOT_STACK_MAGIC) {
        iot_crash_context_t ctx;

        /* 先记录专用原因，随后 IOT_ASSERT 不会覆盖它（只保留第一次） */
        memset(&ctx, 0, sizeof(ctx));
        ctx.expr = "stack overflow";
        (void)iot_crash_record(IOT_CRASH_STACK_OVERFLOW, &ctx);

        LOG_E(IOT_TAG, "stack overflow detected: task=%s size=%u",
                  task->name != NULL ? task->name : "?", (unsigned)task->stack_size);
        IOT_ASSERT(0);
    }
}
#endif

void iot_sched_switch(iot_task_t *next)
{
    iot_task_t *prev = iot_current_task;

    if (next == NULL || next == prev) {
        return;                     /* 仍应运行当前任务，无需切换 */
    }

#if IOT_USE_STACK_CHECK
    iot_stack_guard(prev);
#endif

#if IOT_USE_TIME_SLICING
    next->slice_remaining = IOT_TIME_SLICE_TICKS;
#endif

    iot_current_task = next;
    next->state = IOT_TASK_RUNNING;

#if IOT_USE_STATS
    s_ctx_switches++;
    next->switches++;
#endif

    LOG_D(IOT_TAG, "switch: %s -> %s",
              (prev != NULL && prev->name != NULL) ? prev->name : "(none)",
              next->name != NULL ? next->name : "?");

    iot_port_switch(prev, next);
}

void iot_sched(void)
{
    iot_task_t *next;

    iot_port_critical_enter();
    next = iot_sched_pick();
    iot_port_critical_exit();

    iot_sched_switch(next);
}

/*
 * 中断上下文调度：只做状态迁移，返回需要切入的任务（NULL 表示不用切）。
 * 由移植层的 PendSV 处理程序调用，随后用它完成寄存器切换。
 *
 * 调用者（PendSV 汇编）已全程屏蔽中断，本函数不得再进出临界区：
 * 那会在异常中途执行 cpsie i，使 PendSV 被嵌套、PSP 处于半成品状态。
 */
iot_task_t *iot_sched_from_isr(void)
{
    iot_task_t *prev = iot_current_task;
    iot_task_t *next;

#if IOT_USE_STACK_CHECK
    iot_stack_guard(prev);      /* 抢占路径同样校验栈底魔数 */
#endif

    /* 时间片到期：把被打断的任务放回同优先级队尾，实现轮转 */
    if (s_slice_expired) {
        s_slice_expired = false;
        if (prev != NULL && prev != iot_idle_task &&
            prev->state == IOT_TASK_RUNNING) {
            iot_make_ready(prev);
        }
    }

    next = iot_sched_pick();

    if (next != prev) {
        iot_current_task = next;
        next->state = IOT_TASK_RUNNING;

#if IOT_USE_STATS
        s_ctx_switches++;
        next->switches++;
#endif
    }

    return (next != prev) ? next : NULL;
}

bool iot_sched_preempt_needed(void)
{
    iot_task_t *cur = iot_current_task;
    int prio;

    if (cur == NULL) {
        return false;
    }

    /* 就绪位图最低置位即最高就绪优先级；数值更小表示应抢占 */
    prio = iot_ctz32(s_ready_bitmap);
    return (prio >= 0) && ((uint8_t)prio < cur->priority);
}

#if IOT_USE_TIME_SLICING
void iot_sched_tick_slice(uint32_t delta)
{
    iot_task_t *cur = iot_current_task;

    /* 空闲任务不参与轮转；只有运行态任务消耗时间片 */
    if (cur == NULL || cur == iot_idle_task || cur->state != IOT_TASK_RUNNING) {
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

bool iot_sched_slice_expired(void)
{
    return s_slice_expired;
}

#if IOT_USE_STATS
uint32_t iot_sched_switch_count(void)
{
    return s_ctx_switches;
}

void iot_sched_reset_switch_count(void)
{
    s_ctx_switches = 0U;
}
#endif

iot_task_t *iot_current(void)
{
    return iot_current_task;
}
