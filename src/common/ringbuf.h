/*
 * ringbuf.h - 字节环形缓冲区接口
 *
 * 读写位置为单调递增的 uint32_t 计数，下标按 capacity 取模，
 * 因此容量可为任意正整数。写满时写入操作只接受可用空间内的数据。
 */

#ifndef RINGBUF_H
#define RINGBUF_H

/*
 * 环形缓冲区（字节流，外部提供缓冲区）。
 * 用于 UART DMA / AT / 日志等场景。
 * 读写位置采用单调递增计数，容差任意 capacity（无需 2 的幂）。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 环形缓冲区控制块；buffer/capacity 由调用者提供 */
typedef struct {
    uint8_t *buffer;        /* 外部存储，容量为 capacity 字节 */
    uint32_t capacity;      /* 缓冲容量（字节），任意正整数 */

    volatile uint32_t read_pos;   /* 读位置（单调递增，实际下标需取模） */
    volatile uint32_t write_pos;  /* 写位置（单调递增，实际下标需取模） */
} ringbuf_t;

/* 初始化；返回 0，-1 表示参数非法（指针为 NULL 或 size 为 0） */
int ringbuf_init(ringbuf_t *rb, void *buffer, uint32_t size);

/* 写入数据，返回实际写入字节数（空间不足时只写一部分） */
uint32_t ringbuf_write(ringbuf_t *rb, const void *data, uint32_t len);
/* 读出并移除数据，返回实际读取字节数 */
uint32_t ringbuf_read(ringbuf_t *rb, void *data, uint32_t len);
/* 窥视数据但不移除，返回实际拷贝字节数 */
uint32_t ringbuf_peek(ringbuf_t *rb, void *data, uint32_t len);
/* 丢弃最多 len 字节已读位置的数据，返回实际丢弃字节数 */
uint32_t ringbuf_skip(ringbuf_t *rb, uint32_t len);

/* 可读字节数；rb 为 NULL 返回 0 */
static inline uint32_t ringbuf_available(const ringbuf_t *rb)
{
    return (rb != NULL) ? (rb->write_pos - rb->read_pos) : 0U;
}
/* 剩余可写字节数；rb 为 NULL 返回 0 */
static inline uint32_t ringbuf_free(const ringbuf_t *rb)
{
    return (rb != NULL) ? (rb->capacity - ringbuf_available(rb)) : 0U;
}

/* 清空缓冲区（读写位置归零，丢弃未读数据） */
static inline void ringbuf_reset(ringbuf_t *rb)
{
    if (rb != NULL) {
        rb->read_pos = 0U;
        rb->write_pos = 0U;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* RINGBUF_H */
