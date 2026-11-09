#ifndef IOT_SBUF_H
#define IOT_SBUF_H

/*
 * iot_sbuf.h - 流式字节缓冲（iot_sbuf.c）
 *
 * 定位：面向串口/网络等字节流场景的环形缓冲，介于「定长消息队列」与
 *       「裸环形缓冲」之间：
 *         - 写入/读取按字节计量，不要求消息边界；
 *         - 读写均可阻塞等待，也可用 FromISR 版本在中断中非阻塞访问；
 *         - 有数据/有空间时立即返回实际字节数，只有完全没有数据/空间时才阻塞。
 *
 * 语义：
 *   - iot_sbuf_write() 返回本次实际写入字节数（0..len），空间不足时只写部分；
 *   - iot_sbuf_read()  返回本次实际读取字节数（0..len），数据不足时只读部分；
 *   - 超时返回 0（不视为错误），调用方可按需重试；
 *   - 读走数据会唤醒全部等待写者，写入数据会唤醒全部等待读者。
 *
 * 使用方式：
 *   1) 静态：调用者提供 buffer，iot_sbuf_init(&sb, buf, size)；
 *   2) 动态：iot_sbuf_create(size) / iot_sbuf_destroy()。
 */

#include "iot_types.h"
#include "iot_wait.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iot_sbuf {
    uint8_t *buffer;               /* 字节缓冲区（调用者提供或动态分配） */
    uint32_t size;                 /* 缓冲区容量（字节，非 2 的幂也可） */
    uint32_t head;                 /* 读位置 */
    uint32_t tail;                 /* 写位置 */
    uint32_t count;                /* 当前已缓存字节数 */

    iot_wait_queue_t send_wait;    /* 空间不足时等待的写者 */
    iot_wait_queue_t recv_wait;    /* 数据为空时等待的读者 */

    bool initialized;              /* 初始化标志 */
    bool buffer_owned;             /* 缓冲区是否由 iot_sbuf_create 分配 */
};

#if IOT_USE_SBUF

/* 初始化：buffer 容量为 size 字节，size 必须大于 0 */
int iot_sbuf_init(iot_sbuf_t *sb, void *buffer, uint32_t size);

/*
 * 写入最多 len 字节。
 * 返回：实际写入字节数（>=0）；参数非法返回负错误码。
 * 说明：有空间立即写入并返回；空间为 0 时按 timeout 阻塞等待。
 */
int iot_sbuf_write(iot_sbuf_t *sb, const void *data, uint32_t len,
                   uint32_t timeout);

/*
 * 读取最多 len 字节。
 * 返回：实际读取字节数（>=0）；参数非法返回负错误码。
 * 说明：有数据立即返回；无数据时按 timeout 阻塞等待。
 */
int iot_sbuf_read(iot_sbuf_t *sb, void *data, uint32_t len,
                  uint32_t timeout);

/* 当前缓存字节数 / 剩余空间（字节） */
uint32_t iot_sbuf_count(const iot_sbuf_t *sb);
uint32_t iot_sbuf_space(const iot_sbuf_t *sb);

/* 清空缓冲（丢弃数据），并唤醒等待空间的写者 */
int iot_sbuf_reset(iot_sbuf_t *sb);

/* 动态创建/销毁：结构与缓冲区均由 iot_mem_* 分配 */
iot_sbuf_t *iot_sbuf_create(uint32_t size);

/* 销毁动态缓冲；仍有等待者时返回 IOT_BUSY */
int iot_sbuf_destroy(iot_sbuf_t *sb);

#endif /* IOT_USE_SBUF */

#ifdef __cplusplus
}
#endif

#endif /* IOT_SBUF_H */
