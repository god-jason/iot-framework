/*
 * iot_queue.c - 定长消息队列
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

#include "iot.h"
#include "iot_port.h"
#include "iot_mem.h"

#include <string.h>

/* depth 是否为 2 的幂（用于把取模换成掩码） */
static bool iot_queue_is_pow2(uint32_t v)
{
    return (v != 0U) && ((v & (v - 1U)) == 0U);
}

/* 环形下标前进一格：2 的幂用掩码，否则回退到取模 */
static uint32_t iot_queue_next(const iot_queue_t *queue, uint32_t index)
{
    if (queue->depth_mask != 0U) {
        return (index + 1U) & queue->depth_mask;
    }
    return (index + 1U) % queue->depth;
}

/* 初始化定长消息队列：绑定缓冲、计算掩码并初始化收发等待队列。 */
int iot_queue_init(iot_queue_t *queue, void *buffer,
                              uint32_t item_size, uint32_t depth)
{
    if (queue == NULL || buffer == NULL || item_size == 0U || depth == 0U) {
        LOG_E(IOT_TAG, "queue_init: invalid param");
        return IOT_INVALID_PARAM;
    }

#if IOT_QUEUE_REQUIRE_POW2
    if (!iot_queue_is_pow2(depth)) {
        LOG_E(IOT_TAG, "queue_init: depth %u must be power of two", (unsigned)depth);
        return IOT_INVALID_PARAM;
    }
#endif

    queue->buffer = (uint8_t *)buffer;
    queue->buffer_owned = false;
    queue->item_size = item_size;
    queue->depth = depth;
    queue->depth_mask = iot_queue_is_pow2(depth) ? (depth - 1U) : 0U;
    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 0U;
    iot_wait_queue_init(&queue->send_wait);
    iot_wait_queue_init(&queue->recv_wait);
    queue->initialized = true;

    LOG_D(IOT_TAG, "queue init: item=%u depth=%u",
              (unsigned)item_size, (unsigned)depth);
    return IOT_OK;
}

/*
 * 发送一条消息：队列未满直接入队；已满时与等待的接收者直接握手，
 * 否则按 timeout 阻塞（IOT_TIMEOUT_NONE 立即返回 IOT_TIMEOUT）。
 */
int iot_queue_send(iot_queue_t *queue, const void *item,
                              uint32_t timeout)
{
    iot_task_t *receiver;

    if (!IOT_OBJ_VALID(queue) || item == NULL) {
        LOG_E(IOT_TAG, "queue_send: invalid param");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();

    /* 1) 有接收者在等待：直接交付到其缓冲，避免入队再出队 */
    receiver = iot_wait_queue_pop_highest(&queue->recv_wait);
    if (receiver != NULL) {
        if (receiver->wait_ptr != NULL) {
            memcpy(receiver->wait_ptr, item, queue->item_size);
        }
        iot_task_wake(receiver, IOT_OK);
        iot_port_critical_exit();
        LOG_D(IOT_TAG, "queue send: handoff to %s",
                  receiver->name != NULL ? receiver->name : "?");
        return IOT_OK;
    }

    /* 2) 有剩余空间：入队 */
    if (queue->count < queue->depth) {
        memcpy(queue->buffer + queue->head * queue->item_size,
               item, queue->item_size);
        queue->head = iot_queue_next(queue, queue->head);
        queue->count++;
        iot_port_critical_exit();
        LOG_D(IOT_TAG, "queue send: queued, count=%u", (unsigned)queue->count);
        return IOT_OK;
    }

    iot_port_critical_exit();

    if (timeout == IOT_TIMEOUT_NONE) {
        LOG_D(IOT_TAG, "queue send: full, non-blocking");
        return IOT_TIMEOUT;
    }

    /* 3) 队列满：阻塞等待接收方腾出空间（item 缓冲需保持有效） */
    iot_current_task->wait_ptr = (void *)item;
    LOG_D(IOT_TAG, "queue send: full, blocking timeout=%u", (unsigned)timeout);
    return iot_block_current(&queue->send_wait, timeout);
}

/*
 * 接收一条消息：队列非空直接出队；为空时与等待的发送者直接握手，
 * 否则按 timeout 阻塞（IOT_TIMEOUT_NONE 立即返回 IOT_TIMEOUT）。
 */
int iot_queue_recv(iot_queue_t *queue, void *item, uint32_t timeout)
{
    iot_task_t *sender;

    if (!IOT_OBJ_VALID(queue) || item == NULL) {
        LOG_E(IOT_TAG, "queue_recv: invalid param");
        return IOT_INVALID_PARAM;
    }

    iot_port_critical_enter();

    if (queue->count > 0U) {
        /* 1) 有数据：取出队首 */
        memcpy(item, queue->buffer + queue->tail * queue->item_size,
               queue->item_size);
        queue->tail = iot_queue_next(queue, queue->tail);
        queue->count--;

        /* 腾出一个空位：若有发送者等待，立即把其数据转入队列 */
        sender = iot_wait_queue_pop_highest(&queue->send_wait);
        if (sender != NULL) {
            if (sender->wait_ptr != NULL) {
                memcpy(queue->buffer + queue->head * queue->item_size,
                       sender->wait_ptr, queue->item_size);
            }
            queue->head = iot_queue_next(queue, queue->head);
            queue->count++;
            iot_task_wake(sender, IOT_OK);
        }

        iot_port_critical_exit();
        LOG_D(IOT_TAG, "queue recv: ok, count=%u", (unsigned)queue->count);
        return IOT_OK;
    }

    iot_port_critical_exit();

    if (timeout == IOT_TIMEOUT_NONE) {
        LOG_D(IOT_TAG, "queue recv: empty, non-blocking");
        return IOT_TIMEOUT;
    }

    /* 2) 队列空：阻塞等待发送方 */
    iot_current_task->wait_ptr = item;
    LOG_D(IOT_TAG, "queue recv: empty, blocking timeout=%u", (unsigned)timeout);
    return iot_block_current(&queue->recv_wait, timeout);
}

/*
 * 动态创建消息队列：队列结构与 item_size * depth 的消息缓冲
 * 均由 iot_mem_* 分配，失败返回 NULL。
 */
iot_queue_t *iot_queue_create(uint32_t item_size, uint32_t depth)
{
    iot_queue_t *queue;
    uint32_t total;

    if (item_size == 0U || depth == 0U ||
        depth > ((uint32_t)-1) / item_size) {
        return NULL;
    }
    total = item_size * depth;

    queue = (iot_queue_t *)iot_mem_calloc(1U, sizeof(*queue));
    if (queue == NULL) {
        LOG_E(IOT_TAG, "queue_create: no memory for queue");
        return NULL;
    }
    queue->buffer = (uint8_t *)iot_mem_malloc(total);
    if (queue->buffer == NULL) {
        LOG_E(IOT_TAG, "queue_create: no memory for buffer");
        iot_mem_free(queue);
        return NULL;
    }

    if (iot_queue_init(queue, queue->buffer, item_size, depth) != IOT_OK) {
        iot_mem_free(queue->buffer);
        iot_mem_free(queue);
        return NULL;
    }
    queue->buffer_owned = true;
    return queue;
}

/*
 * 销毁动态队列：仍有消息或收发等待者时返回 IOT_BUSY；
 * 成功时释放消息缓冲（若是动态分配）与队列对象。
 */
int iot_queue_destroy(iot_queue_t *queue)
{
    if (!IOT_OBJ_VALID(queue)) {
        return IOT_INVALID_PARAM;
    }

    /* 队列内仍有消息时同样视为忙；与失效标记放在同一临界区 */
    iot_port_critical_enter();
    if (queue->count != 0U ||
        !iot_wait_queue_empty(&queue->send_wait) ||
        !iot_wait_queue_empty(&queue->recv_wait)) {
        iot_port_critical_exit();
        LOG_W(IOT_TAG, "queue_destroy: not empty");
        return IOT_BUSY;
    }
    queue->initialized = false;
    iot_port_critical_exit();

    if (queue->buffer_owned) {
        iot_mem_free(queue->buffer);
    }
    iot_mem_free(queue);
    return IOT_OK;
}