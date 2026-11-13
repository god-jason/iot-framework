/*
 * ros_event.c - 事件组实现
 *
 * 等待者挂在 ros_wait_queue_t 上（按优先级分链）。set() 时按优先级从高到低
 * 遍历所有等待者，先收集所有满足者的清位掩码，唤醒完成后再统一清位，
 * 避免多个 ROS_EVENT_CLEAR 等待者之间互相抢走标志位。
 *
 * 等待采用「检查—阻塞—重查」循环：被唤醒后条件可能已被其他等待者清掉，
 * 永久等待时继续阻塞，有限超时则不再续等（内核不记录剩余 tick）。
 */

#include "ros.h"
#include "common.h"
#include "ros_port.h"
#include "ros_mem.h"

#if ROS_USE_EVENT

/* 判断当前标志是否满足等待条件：ALL 要求全部置位，ANY 任一置位即可。 */
static bool ros_event_match(uint32_t flags, uint32_t bits, uint8_t options)
{
    if ((options & ROS_EVENT_ALL) != 0U) {
        return (flags & bits) == bits;
    }
    return (flags & bits) != 0U;
}

/* 初始化事件组：标志位清零并初始化等待队列。 */
int ros_event_init(ros_event_t *event)
{
    if (event == NULL) {
        return ROS_INVALID_PARAM;
    }

    event->flags = 0U;
    ros_wait_queue_init(&event->wait);
    event->initialized = true;
    return ROS_OK;
}

/*
 * 置位事件：先顺序唤醒所有因此满足等待条件的任务，
 * 唤醒完成后再统一应用 ROS_EVENT_CLEAR，避免等待者互相抢位。
 */
int ros_event_set(ros_event_t *event, uint32_t bits)
{
    uint32_t bitmap;
    uint32_t clear_mask = 0U;

    if (!ROS_OBJ_VALID(event)) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    event->flags |= bits;

    /* 用位图快照遍历：唤醒会摘链并修改 wait.bitmap */
    bitmap = event->wait.bitmap;
    while (bitmap != 0U) {
        uint8_t prio = ros_wait_queue_first_prio(bitmap);
        list_t *chain;
        list_node_t *node;
        list_node_t *next;

        bitmap &= ~(1U << prio);
        chain = &event->wait.prio[prio];

        for (node = chain->head.next; node != &chain->head; node = next) {
            ros_task_t *task = ROS_CONTAINER_OF(node, ros_task_t, wait_node);

            next = node->next;   /* 唤醒会摘链，先取后继 */
            if (ros_event_match(event->flags, task->wait_bits, task->wait_opt)) {
                if ((task->wait_opt & ROS_EVENT_CLEAR) != 0U) {
                    clear_mask |= task->wait_bits;
                }
                ros_task_wake(task, ROS_OK);
            }
        }
    }

    /* 统一清位，保证所有满足者看到同一份标志快照 */
    event->flags &= ~clear_mask;
    ros_port_critical_exit();

    ROS_TRACE_OBJ(ROS_EVT_EVENT_SET, (uint32_t)(uintptr_t)event, bits);
    return ROS_OK;
}

/* 清除指定事件位（不会影响等待者，仅更新标志）。 */
int ros_event_clear(ros_event_t *event, uint32_t bits)
{
    if (!ROS_OBJ_VALID(event)) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    event->flags &= ~bits;
    ros_port_critical_exit();
    return ROS_OK;
}

/* 读取当前事件标志位。 */
int ros_event_get(ros_event_t *event, uint32_t *flags)
{
    if (!ROS_OBJ_VALID(event) || flags == NULL) {
        return ROS_INVALID_PARAM;
    }

    ros_port_critical_enter();
    *flags = event->flags;
    ros_port_critical_exit();
    return ROS_OK;
}

/*
 * 等待事件位：条件已满足立即返回；否则按 timeout 阻塞。
 * 被唤醒后重查条件（可能被其他等待者清位），有限超时只等待一次。
 */
int ros_event_wait(ros_event_t *event, uint32_t bits, uint8_t options,
                   uint32_t timeout, uint32_t *out_flags)
{
    ros_task_t *cur;
    uint32_t deadline = ros_tick_get() + timeout;

    if (!ROS_OBJ_VALID(event) || bits == 0U) {
        return ROS_INVALID_PARAM;
    }

    cur = ros_current_task;
    if (cur == NULL) {
        return ROS_NOT_READY;
    }

    for (;;) {
        ros_port_critical_enter();
        if (ros_event_match(event->flags, bits, options)) {
            if (out_flags != NULL) {
                *out_flags = event->flags;
            }
            if ((options & ROS_EVENT_CLEAR) != 0U) {
                event->flags &= ~bits;
            }
            ros_port_critical_exit();
            ROS_TRACE_OBJ(ROS_EVT_EVENT_WAIT, (uint32_t)(uintptr_t)event, bits);
            return ROS_OK;
        }
        ros_port_critical_exit();

        cur->wait_bits = bits;
        cur->wait_opt = options;

        /* 被唤醒后重查条件：可能已被其他等待者清位，按剩余时间续等 */
        {
            int rc = ros_wait_retry(&event->wait, deadline, timeout);

            if (rc != ROS_OK) {
                return rc;      /* ROS_TIMEOUT / ROS_FORBIDDEN_IN_ISR */
            }
        }
    }
}

/* 动态创建事件组：对象由 ros_mem_calloc 分配，失败返回 NULL。 */
ros_event_t *ros_event_create(void)
{
    ros_event_t *event = (ros_event_t *)ros_mem_calloc(1U, sizeof(*event));

    if (event == NULL) {
        LOG_E(ROS_TAG, "event_create: no memory");
        return NULL;
    }
    if (ros_event_init(event) != ROS_OK) {
        ros_mem_free(event);
        return NULL;
    }
    return event;
}

/*
 * 销毁动态事件组：仍有任务等待时返回 ROS_BUSY；
 * 成功时标记失效并释放对象内存。
 */
int ros_event_destroy(ros_event_t *event)
{
    if (!ROS_OBJ_VALID(event)) {
        return ROS_INVALID_PARAM;
    }

    if (ros_obj_destroy_begin(&event->initialized, &event->wait, NULL) != ROS_OK) {
        LOG_W(ROS_TAG, "event_destroy: still has waiters");
        return ROS_BUSY;
    }

    ros_mem_free(event);
    return ROS_OK;
}

#else /* ROS_USE_EVENT */

typedef int ros_event_unused_t;

#endif /* ROS_USE_EVENT */
