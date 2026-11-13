/*
 * ros_tick.c - 内核 tick 与延时链表
 *
 * s_delayed 为按 wake_tick 升序排列的延时链表：
 *   - 睡眠任务（ROS_TASK_DELAYED）；
 *   - 带超时阻塞的任务（ROS_TASK_BLOCKED，同时还在对象等待链表上）。
 *
 * 唤醒策略：
 *   到期任务先在临界区内摘链并暂存到局部链表，退出临界区后再逐个唤醒，
 *   把“关中断时间”从 O(到期任务数 × 链表操作) 降为 O(到期任务数)；
 *   同时累加运行统计并推进时间片。
 */

#include "ros.h"
#include "common.h"
#include "ros_port.h"

uint32_t ros_tick_count = 0;

static list_t s_delayed;

/* 延时链表节点 → 任务 */
static ros_task_t *ros_delayed_task(list_node_t *node)
{
    return ROS_CONTAINER_OF(node, ros_task_t, delayed_node);
}

/* 初始化 tick 计数与延时链表（由 ros_init 调用）。 */
void ros_tick_init(void)
{
    list_init(&s_delayed);
    ros_tick_count = 0U;
}

/*
 * 把任务挂入延时链表（按 wake_tick 升序），
 * 调用者必须已持有临界区（阻塞/睡眠路径均在同一临界区内调用）。
 */
void ros_delayed_add(ros_task_t *task)
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
        !ROS_TICK_AFTER(tail->sort_key, node->sort_key)) {
        list_add_tail(&s_delayed, node);
        return;
    }

    list_insert_sorted(&s_delayed, node);
}

/* 返回延时链表最早唤醒时刻；链表为空返回 UINT32_MAX。 */
uint32_t ros_delayed_next_deadline(void)
{
    list_node_t *node = list_first(&s_delayed);

    if (node == NULL) {
        return UINT32_MAX;
    }
    return ros_delayed_task(node)->wake_tick;
}

/*
 * 推进内核时间：
 *   1) 摘下所有到期任务并在临界区外逐个唤醒；
 *   2) 累计当前任务运行 tick、消耗时间片；
 *   3) 处理到期软件定时器；
 *   4) 时间片耗尽时请求重调度。
 */
void ros_tick_increment(uint32_t delta)
{
    list_t expired;
    list_node_t *node;
    ros_task_t *task;

    if (delta == 0U) {
        return;
    }

    list_init(&expired);

    ROS_TRACE_DBG(ROS_EVT_TICK, ros_tick_count + delta, delta);

    ros_port_critical_enter();
    ros_tick_count += delta;

    /* 1) 摘下所有到期任务（此时仍持有临界区，链表不会被并发修改） */
    for (;;) {
        node = list_first(&s_delayed);
        if (node == NULL) {
            break;
        }

        task = ros_delayed_task(node);
        if (ROS_TICK_AFTER(task->wake_tick, ros_tick_count)) {
            break;                  /* 后续任务尚未到期 */
        }

        list_remove(&task->delayed_node);

        if (task->state == ROS_TASK_BLOCKED) {
            /* 带超时阻塞：从 O(1) 等待队列摘除并返回超时结果 */
            if (task->wait_queue != NULL) {
                ros_wait_queue_remove(task->wait_queue, task);
            }
            task->wait_result = ROS_TIMEOUT;
        } else {
            task->wait_result = ROS_OK;
        }

        /* 暂存到局部链表，借用 delayed_node（已摘下，处于游离态） */
        list_add_tail(&expired, &task->delayed_node);
    }

    /* 2) 运行统计：当前任务累计运行 tick（含空闲任务，占比之和约 100%） */
#if ROS_USE_STATS
    if (ros_current_task != NULL &&
        ros_current_task->state == ROS_TASK_RUNNING) {
        ros_current_task->run_ticks += delta;
    }
#endif

    /* 3) 时间片消耗 */
#if ROS_USE_TIME_SLICING
    ros_sched_tick_slice(delta);
#endif

    ros_port_critical_exit();

    /* 4) 临界区外逐个唤醒，避免长时间关中断 */
    while ((node = list_pop_first(&expired)) != NULL) {
        int rc;

        task = ROS_CONTAINER_OF(node, ros_task_t, delayed_node);
        rc = task->wait_result;
        ros_task_wake(task, rc);

        if (rc == ROS_TIMEOUT) {
            /* 因超时到期：对象等待链已摘除，结果为 ROS_TIMEOUT */
            ROS_TRACE(ROS_EVT_TASK_TIMEOUT, (uint32_t)(uintptr_t)task,
                      ros_tick_count);
        }

        LOG_D(ROS_TAG, "timeout/wake: task=%s tick=%u",
                  task->name != NULL ? task->name : "?",
                  (unsigned)ros_tick_count);
    }

    /* 5) 处理软件定时器（回调在调用者上下文执行） */
    ros_timer_tick(ros_tick_count);

    /* 6) 时间片耗尽：请求一次重新调度（MCU 端触发 PendSV） */
#if ROS_USE_TIME_SLICING
    if (ros_sched_slice_expired()) {
        ROS_TRACE(ROS_EVT_SLICE_EXPIRE, (uint32_t)(uintptr_t)ros_current_task,
                  ros_tick_count);
        ros_port_request_switch();
    }
#endif
}

/* 推进 1 个 tick（SysTick/模拟端周期调用）。 */
void ros_tick(void)
{
    ros_tick_increment(1U);
}

/* 读取当前 tick 计数（32 位，允许回绕）。 */
uint32_t ros_tick_get(void)
{
    return ros_tick_count;
}

/*
 * 计算空闲任务可休眠的 tick 数：取延时链表与软件定时器的最近截止点；
 * 没有任何截止点返回 UINT32_MAX，已到期返回 1。
 */
uint32_t ros_idle_next_timeout(void)
{
    uint32_t now = ros_tick_count;
    uint32_t delayed = ros_delayed_next_deadline();
    uint32_t timer = ros_timer_next_deadline();
    uint32_t next = (delayed < timer) ? delayed : timer;

    if (next == UINT32_MAX) {
        return UINT32_MAX;          /* 没有任何截止点 */
    }
    if (ROS_TICK_AFTER(next, now)) {
        return next - now;
    }
    return 1U;                      /* 已到期：推进 1 tick 触发处理 */
}
