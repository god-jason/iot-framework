/*
 * queue.c - 定长消息队列
 *
 * 环形缓冲：
 *   head 为下一个写入位置，tail 为下一个读取位置，count 单独维护，
 *   因此“空”和“满”都表现为 head == tail，需用 count 区分。
 *
 * 阻塞语义：
 *   - 发送时若队列满：挂入 send_wait，等待接收方腾出空间；
 *   - 接收时若队列空：挂入 recv_wait，等待发送方写入；
 *   - 发送/接收会优先与对方等待者直接握手，避免多余入队出队。
 */

#include "internal.h"
#include "port.h"

#include <string.h>

iot_status_t iot_queue_init(iot_queue_t *queue, void *buffer,
                              uint32_t item_size, uint32_t depth)
{
    if (queue == NULL || buffer == NULL || item_size == 0U || depth == 0U) {
        LOG_E("iot", "queue_init: invalid param");
        return IOT_INVALID_PARAM;
    }

    queue->buffer = (uint8_t *)buffer;
    queue->item_size = item_size;
    queue->depth = depth;
    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 0U;
    list_init(&queue->send_wait);
    list_init(&queue->recv_wait);
    queue->initialized = true;

    LOG_D("iot", "queue init: item=%u depth=%u",
              (unsigned)item_size, (unsigned)depth);
    return IOT_OK;
}

iot_status_t iot_queue_send(iot_queue_t *queue, const void *item,
                              uint32_t timeout)
{
    iot_task_t *receiver;

    if (queue == NULL || !queue->initialized || item == NULL) {
        LOG_E("iot", "queue_send: invalid param");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();

    /* 1) 有接收者在等待：直接交付到其缓冲，避免入队再出队 */
    receiver = iot_wait_pop_highest(&queue->recv_wait);
    if (receiver != NULL) {
        if (receiver->wait_ptr != NULL) {
            memcpy(receiver->wait_ptr, item, queue->item_size);
        }
        iot_task_wake(receiver, IOT_OK);
        iot_port_critical_exit();
        LOG_D("iot", "queue send: handoff to %s",
                  receiver->name != NULL ? receiver->name : "?");
        return IOT_OK;
    }

    /* 2) 有剩余空间：入队 */
    if (queue->count < queue->depth) {
        memcpy(queue->buffer + queue->head * queue->item_size,
               item, queue->item_size);
        queue->head = (queue->head + 1U) % queue->depth;
        queue->count++;
        iot_port_critical_exit();
        LOG_D("iot", "queue send: queued, count=%u", (unsigned)queue->count);
        return IOT_OK;
    }

    iot_port_critical_exit();

    if (timeout == IOT_TIMEOUT_NONE) {
        LOG_D("iot", "queue send: full, non-blocking");
        return IOT_TIMEOUT;
    }

    /* 3) 队列满：阻塞等待接收方腾出空间（item 缓冲需保持有效） */
    iot_current_task->wait_ptr = (void *)item;
    LOG_D("iot", "queue send: full, blocking timeout=%u", (unsigned)timeout);
    return iot_block_current(&queue->send_wait, timeout);
}

iot_status_t iot_queue_recv(iot_queue_t *queue, void *item, uint32_t timeout)
{
    iot_task_t *sender;

    if (queue == NULL || !queue->initialized || item == NULL) {
        LOG_E("iot", "queue_recv: invalid param");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();

    if (queue->count > 0U) {
        /* 1) 有数据：取出队首 */
        memcpy(item, queue->buffer + queue->tail * queue->item_size,
               queue->item_size);
        queue->tail = (queue->tail + 1U) % queue->depth;
        queue->count--;

        /* 腾出一个空位：若有发送者等待，立即把其数据转入队列 */
        sender = iot_wait_pop_highest(&queue->send_wait);
        if (sender != NULL) {
            if (sender->wait_ptr != NULL) {
                memcpy(queue->buffer + queue->head * queue->item_size,
                       sender->wait_ptr, queue->item_size);
            }
            queue->head = (queue->head + 1U) % queue->depth;
            queue->count++;
            iot_task_wake(sender, IOT_OK);
        }

        iot_port_critical_exit();
        LOG_D("iot", "queue recv: ok, count=%u", (unsigned)queue->count);
        return IOT_OK;
    }

    iot_port_critical_exit();

    if (timeout == IOT_TIMEOUT_NONE) {
        LOG_D("iot", "queue recv: empty, non-blocking");
        return IOT_TIMEOUT;
    }

    /* 2) 队列空：阻塞等待发送方 */
    iot_current_task->wait_ptr = item;
    LOG_D("iot", "queue recv: empty, blocking timeout=%u", (unsigned)timeout);
    return iot_block_current(&queue->recv_wait, timeout);
}