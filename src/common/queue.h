/*
 * queue.h - 定长 FIFO 环形队列接口
 *
 * 采用数组 + head/tail 取模的循环队列，按值拷贝元素；
 * 容量由调用者提供的缓冲区决定，满时入队失败（不覆盖）。
 */

#ifndef QUEUE_H
#define QUEUE_H

/*
 * 定长队列（数组实现，外部提供缓冲区，无动态内存）。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 队列控制块与存储描述 */
typedef struct {
    uint8_t *buffer;      /* 外部缓冲区，容量至少 item_size * capacity */
    uint32_t item_size;   /* 单元素字节数 */
    uint32_t capacity;    /* 最大元素个数 */

    uint32_t head;        /* 队首索引（下一个出队位置） */
    uint32_t tail;        /* 队尾索引（下一个入队位置） */
    uint32_t count;       /* 当前元素个数 */
} queue_t;

/*
 * 初始化队列。
 * 参数：queue - 队列对象；buffer - 外部存储；
 *       item_size - 单元素字节数（> 0）；capacity - 元素容量（> 0）
 * 返回：0 成功；-1 参数非法
 */
int queue_init(queue_t *queue, void *buffer,
                   uint32_t item_size, uint32_t capacity);

/* 入队（拷贝 item_size 字节）；返回 0，-1 表示参数非法或队列已满 */
int queue_push(queue_t *queue, const void *item);
/* 出队并拷贝到 item；返回 0，-1 表示参数非法或队列为空 */
int queue_pop(queue_t *queue, void *item);
/* 仅查看队首元素（不出队）；返回 0，-1 表示参数非法或队列为空 */
int queue_peek(queue_t *queue, void *item);

/* 判空；queue 为 NULL 返回 true */
bool queue_empty(const queue_t *queue);
/* 判满；queue 为 NULL 返回 false */
bool queue_full(const queue_t *queue);

/* 当前元素个数；queue 为 NULL 返回 0 */
uint32_t queue_size(const queue_t *queue);
/* 清空队列（不清零缓冲区内容） */
void queue_clear(queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_H */
