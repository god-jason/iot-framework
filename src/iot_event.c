/*
 * iot_event.c - 事件组实现
 *
 * 等待者挂在 iot_wait_queue_t 上（按优先级分链）。set() 时按优先级从高到低
 * 遍历所有等待者，先收集所有满足者的清位掩码，唤醒完成后再统一清位，
 * 避免多个 IOT_EVENT_CLEAR 等待者之间互相抢走标志位。
 *
 * 等待采用「检查—阻塞—重查」循环：被唤醒后条件可能已被其他等待者清掉，
 * 永久等待时继续阻塞，有限超时则不再续等（内核不记录剩余 tick）。
 */

#include "iot.h"
#include "iot_port.h"

#if IOT_USE_EVENT

static bool iot_event_match(uint32_t flags, uint32_t bits, uint8_t options)
{
    if ((options & IOT_EVENT_ALL) != 0U) {
        return (flags & bits) == bits;
    }
    return (flags & bits) != 0U;
}

int iot_event_init(iot_event_t *event)
{
    if (event == NULL) {
        return IOT_INVALID_PARAM;
    }

    event->flags = 0U;
    iot_wait_queue_init(&event->wait);
    event->initialized = true;
    return IOT_OK;
}

int iot_event_set(iot_event_t *event, uint32_t bits)
{
    uint32_t bitmap;
    uint32_t clear_mask = 0U;

    if (!IOT_OBJ_VALID(event)) {
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    event->flags |= bits;

    /* 用位图快照遍历：唤醒会摘链并修改 wait.bitmap */
    bitmap = event->wait.bitmap;
    while (bitmap != 0U) {
        uint8_t prio = iot_wait_queue_first_prio(bitmap);
        list_t *chain;
        list_node_t *node;
        list_node_t *next;

        bitmap &= ~(1U << prio);
        chain = &event->wait.prio[prio];

        for (node = chain->head.next; node != &chain->head; node = next) {
            iot_task_t *task = IOT_CONTAINER_OF(node, iot_task_t, wait_node);

            next = node->next;   /* 唤醒会摘链，先取后继 */
            if (iot_event_match(event->flags, task->wait_bits, task->wait_opt)) {
                if ((task->wait_opt & IOT_EVENT_CLEAR) != 0U) {
                    clear_mask |= task->wait_bits;
                }
                iot_task_wake(task, IOT_OK);
            }
        }
    }

    /* 统一清位，保证所有满足者看到同一份标志快照 */
    event->flags &= ~clear_mask;
    iot_port_critical_exit();

    return IOT_OK;
}

int iot_event_clear(iot_event_t *event, uint32_t bits)
{
    if (!IOT_OBJ_VALID(event)) {
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    event->flags &= ~bits;
    iot_port_critical_exit();
    return IOT_OK;
}

int iot_event_get(iot_event_t *event, uint32_t *flags)
{
    if (!IOT_OBJ_VALID(event) || flags == NULL) {
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();
    *flags = event->flags;
    iot_port_critical_exit();
    return IOT_OK;
}

int iot_event_wait(iot_event_t *event, uint32_t bits, uint8_t options,
                   uint32_t timeout, uint32_t *out_flags)
{
    iot_task_t *cur;
    uint32_t remaining = timeout;

    if (!IOT_OBJ_VALID(event) || bits == 0U) {
        return IOT_INVALID_PARAM;
    }

    cur = iot_current_task;
    if (cur == NULL) {
        return IOT_NOT_READY;
    }

    for (;;) {
        int rc;

        iot_port_critical_enter();
        if (iot_event_match(event->flags, bits, options)) {
            if (out_flags != NULL) {
                *out_flags = event->flags;
            }
            if ((options & IOT_EVENT_CLEAR) != 0U) {
                event->flags &= ~bits;
            }
            iot_port_critical_exit();
            return IOT_OK;
        }
        iot_port_critical_exit();

        if (remaining == IOT_TIMEOUT_NONE) {
            return IOT_TIMEOUT;
        }

        cur->wait_bits = bits;
        cur->wait_opt = options;

        rc = iot_block_current(&event->wait, remaining);
        if (rc != IOT_OK) {
            return rc;      /* 超时 */
        }

        /* 被唤醒后重查；永久等待可继续，有限超时不重复计时 */
        remaining = (timeout == IOT_TIMEOUT_FOREVER)
                    ? IOT_TIMEOUT_FOREVER : IOT_TIMEOUT_NONE;
    }
}

#else /* IOT_USE_EVENT */

typedef int iot_event_unused_t;

#endif /* IOT_USE_EVENT */
