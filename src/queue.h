#ifndef IOT_QUEUE_H
#define IOT_QUEUE_H

/*
 * queue.h - 定长消息队列（queue.c）
 *
 * 环形缓冲 + 阻塞收发，发送/接收优先与对方等待者直接握手。
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 初始化定长消息队列。
 *   buffer    : 队列缓冲（调用者提供），容量 = item_size * depth；
 *   item_size : 单条消息大小（字节）；
 *   depth     : 队列深度（条数）。
 */
iot_status_t iot_queue_init(iot_queue_t *queue, void *buffer,
                              uint32_t item_size, uint32_t depth);

/* 发送一条消息；队列满且 timeout 不为 0 时阻塞（item 缓冲须保持有效） */
iot_status_t iot_queue_send(iot_queue_t *queue, const void *item,
                              uint32_t timeout);

/* 接收一条消息；队列空且 timeout 不为 0 时阻塞 */
iot_status_t iot_queue_recv(iot_queue_t *queue, void *item,
                              uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* IOT_QUEUE_H */
