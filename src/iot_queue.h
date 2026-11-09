#ifndef IOT_QUEUE_H
#define IOT_QUEUE_H

/*
 * iot_queue.h - 定长消息队列（iot_queue.c）
 *
 * 环形缓冲 + 阻塞收发，发送/接收优先与对方等待者直接握手。
 */

#include "iot_types.h"
#include "iot_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iot_queue {
    uint8_t *buffer;                /* 消息缓冲区（调用者提供或动态分配） */
    bool buffer_owned;              /* 缓冲区是否由 iot_queue_create 分配 */
    uint32_t item_size;             /* 单条消息大小（字节） */
    uint32_t depth;                 /* 队列深度（条数） */
    uint32_t depth_mask;            /* depth 为 2 的幂时的掩码，否则为 0（回退取模） */
    uint32_t head;                  /* 下一个写入位置 */
    uint32_t tail;                  /* 下一个读取位置 */
    uint32_t count;                 /* 当前消息数 */

    iot_wait_queue_t send_wait;     /* 队列满时等待发送的任务 */
    iot_wait_queue_t recv_wait;     /* 队列空时等待接收的任务 */

    bool initialized;               /* 初始化标志 */
};

/*
 * 初始化定长消息队列。
 *   buffer    : 队列缓冲（调用者提供），容量 = item_size * depth；
 *   item_size : 单条消息大小（字节）；
 *   depth     : 队列深度（条数）。
 */
int iot_queue_init(iot_queue_t *queue, void *buffer,
                   uint32_t item_size, uint32_t depth);

/*
 * 动态创建/销毁：队列结构与消息缓冲区均由 iot_mem_* 分配。
 * 内存需求 = sizeof(iot_queue_t) + item_size * depth，失败返回 NULL。
 */
iot_queue_t *iot_queue_create(uint32_t item_size, uint32_t depth);

/* 销毁动态队列；仍有消息或收发等待者时返回 IOT_BUSY */
int iot_queue_destroy(iot_queue_t *queue);

/*
 * 发送一条消息；队列满且 timeout 不为 0 时阻塞。
 * 阻塞期间 item 指向的缓冲必须保持有效（通常为静态或长期存活内存），
 * 直到本次调用返回；中断上下文请使用 iot_queue_send_from_isr()。
 */
int iot_queue_send(iot_queue_t *queue, const void *item, uint32_t timeout);

/* 接收一条消息；队列空且 timeout 不为 0 时阻塞 */
int iot_queue_recv(iot_queue_t *queue, void *item, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* IOT_QUEUE_H */
