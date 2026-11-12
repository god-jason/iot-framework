#ifndef ROS_MSGQ_H
#define ROS_MSGQ_H

/*
 * ros_msgq.h - 变长消息队列（ros_msgq.c）
 *
 * 与 ros_queue.h 的区别：
 *   - ros_queue  ：定长消息，每条 item_size 字节，适合结构体/命令字；
 *   - ros_msgq   ：变长消息，每条自带长度头，适合长度不定的协议帧/JSON 片段；
 *   - ros_sbuf    ：纯字节流，不保留消息边界。
 *
 * 存储：环形字节缓冲，每条消息格式为 [4 字节长度][payload]，
 *       消息可在缓冲末尾回绕；总空间不足时整条消息不写入（不会写半条）。
 *
 * 语义：
 *   - send 成功返回 ROS_OK；空间不足且超时返回 ROS_TIMEOUT；
 *   - recv 成功返回 ROS_OK，并写入消息长度；
 *   - recv 缓冲不足时不消费消息，返回 ROS_OVERFLOW，out_len 返回所需长度；
 *   - out_len 可空；msg 为 NULL 且 max_len 为 0 时仅查询队首消息长度。
 */

#include "ros_types.h"
#include "ros_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 每条消息的长度头字节数 */
#define ROS_MSGQ_HDR_SIZE 4U

struct ros_msgq {
    uint8_t *buffer;               /* 环形字节缓冲（调用者提供或动态分配） */
    uint32_t size;                 /* 缓冲容量（字节） */
    uint32_t head;                 /* 队首（读位置） */
    uint32_t tail;                 /* 队尾（写位置） */
    uint32_t used;                 /* 已用字节数（含消息头） */
    uint32_t count;                /* 当前消息条数 */

    ros_wait_queue_t send_wait;    /* 空间不足时等待的发送者 */
    ros_wait_queue_t recv_wait;    /* 队列为空时等待的接收者 */

    bool initialized;              /* 初始化标志 */
    bool buffer_owned;             /* 缓冲区是否由 ros_msgq_create 分配 */
};

#if ROS_USE_MSGQ

/*
 * 初始化变长消息队列。
 *   buffer : 调用者提供的字节缓冲；
 *   size   : 容量（字节），必须大于 ROS_MSGQ_HDR_SIZE。
 */
int ros_msgq_init(ros_msgq_t *mq, void *buffer, uint32_t size);

/*
 * 发送一条变长消息。
 *   len : 消息字节数，必须 >0 且 <= size - ROS_MSGQ_HDR_SIZE。
 * 返回：ROS_OK / ROS_TIMEOUT（空间不足）/ ROS_INVALID_PARAM / ROS_FORBIDDEN_IN_ISR。
 */
int ros_msgq_send(ros_msgq_t *mq, const void *msg, uint32_t len,
                  uint32_t timeout);

/*
 * 接收一条消息。
 *   max_len : 接收缓冲容量；
 *   out_len : 可空，返回消息实际长度（缓冲不足时也返回所需长度）。
 * 返回：ROS_OK / ROS_OVERFLOW（缓冲不足，消息保留）/ ROS_TIMEOUT / ROS_INVALID_PARAM。
 */
int ros_msgq_recv(ros_msgq_t *mq, void *msg, uint32_t max_len,
                  uint32_t *out_len, uint32_t timeout);

/* 当前消息条数 / 已用字节数 */
uint32_t ros_msgq_count(const ros_msgq_t *mq);
uint32_t ros_msgq_bytes(const ros_msgq_t *mq);

/* 清空队列（丢弃全部消息），并唤醒等待空间的发送者 */
int ros_msgq_reset(ros_msgq_t *mq);

/* 动态创建/销毁：结构与缓冲区均由 ros_mem_* 分配 */
ros_msgq_t *ros_msgq_create(uint32_t size);

/* 销毁动态队列；仍有等待者时返回 ROS_BUSY */
int ros_msgq_destroy(ros_msgq_t *mq);

#endif /* ROS_USE_MSGQ */

#ifdef __cplusplus
}
#endif

#endif /* ROS_MSGQ_H */
