/*
 * sched.c - 就绪表与调度器
 *
 * 数据结构：
 *   - 每个优先级一条环形链表 s_ready[prio]；
 *   - s_ready_bitmap 的 bit i 为 1 表示优先级 i 有就绪任务；
 *   - 取最高优先级就绪任务 = 求 bitmap 最低置位（ctz）。
 *
 * 运行中的任务不在就绪表上：
 *   让出/睡眠/阻塞前由调用方先把任务归位（就绪表或等待/延时链表）。
 */

#include "iot.h"
#include "port.h"

/* 调度器拥有的全局状态 */
iot_task_t *iot_current_task = NULL;

static uint32_t s_ready_bitmap;
static list_t s_ready[IOT_MAX_PRIORITY];

/* ------------------------------ 内部工具 ------------------------------ */

/*
 * 求 32 位整数最低置位的位置（0..31）。
 * 返回 -1 表示 v 为 0。使用 ctz 语义，优先级 0 最高。
 */
static int iot_ctz32(uint32_t v)
{
    int n = 0;

    if (v == 0U) {
        return -1;
    }
    while ((v & 1U) == 0U) {
        v >>= 1;
        n++;
    }
    return n;
}

/* ------------------------------ 就绪表 ------------------------------ */

void iot_sched_init(void)
{
    int i;

    for (i = 0; i < IOT_MAX_PRIORITY; i++) {
        list_init(&s_ready[i]);
    }
    s_ready_bitmap = 0U;
    iot_current_task = NULL;
}

void iot_make_ready(iot_task_t *task)
{
    uint8_t prio;

    if (task == NULL || task->state == IOT_TASK_TERMINATED) {
        return;
    }

    prio = task->priority;
    if (prio >= IOT_MAX_PRIORITY) {
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
        /* 位图与链表不一致，清除该位自愈 */
        s_ready_bitmap &= ~(1U << prio);
        return NULL;
    }

    if (list_empty(&s_ready[prio])) {
        s_ready_bitmap &= ~(1U << prio);
    }

    task = IOT_CONTAINER_OF(node, iot_task_t, ready_node);
    task->state = IOT_TASK_RUNNING;
    return task;
}

/* ------------------------------ 调度 ------------------------------ */

iot_task_t *iot_sched_pick(void)
{
    iot_task_t *next = iot_sched_select();

    return (next != NULL) ? next : iot_idle_task;
}

void iot_sched_switch(iot_task_t *next)
{
    iot_task_t *prev = iot_current_task;

    if (next == NULL || next == prev) {
        return;                     /* 仍应运行当前任务，无需切换 */
    }

    iot_current_task = next;
    next->state = IOT_TASK_RUNNING;

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

iot_task_t *iot_current(void)
{
    return iot_current_task;
}