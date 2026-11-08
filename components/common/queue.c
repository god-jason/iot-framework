/*
 * queue.c - 定长环形队列（数组实现）
 *
 * 模块职责：
 *   提供按元素定长的 FIFO 队列，用于任务间传递小对象或事件。
 *   所有存储由调用者提供，模块不分配内存。
 *
 * 数据流与依赖：
 *   buffer 被划分为 capacity 个 item_size 大小的槽位；
 *   head 指向队首（出队位置）、tail 指向队尾（入队位置），
 *   两者按 capacity 取模循环，count 记录当前元素个数。
 *   仅依赖 标准头文件 与 memcpy/memcmp 无关的 memcpy。
 *
 * 边界约束：
 *   - buffer 容量必须 >= item_size * capacity，且按元素类型对齐。
 *   - 队列满时 queue_push() 返回 -1（不做覆盖），空时 pop/peek 返回 -1。
 *   - 采用“按值拷贝”，元素必须是可平凡拷贝的数据（不可含指针所有权语义）。
 *   - 非线程安全，多任务并发访问需外部加锁。
 */

#include "queue.h"
#include <string.h>

/*
 * 初始化队列。
 * 参数：queue - 队列对象；buffer - 外部缓冲区；
 *       item_size - 单元素字节数（> 0）；capacity - 元素容量（> 0）
 * 返回：0 成功；-1 参数非法
 * 注意：不初始化缓冲区内容，仅重置读写位置与计数。
 */
int queue_init(queue_t *queue, void *buffer,
                   uint32_t item_size, uint32_t capacity)
{
    if (queue == NULL || buffer == NULL || item_size == 0 || capacity == 0) {
        return -1;
    }
    queue->buffer = (uint8_t *)buffer;
    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    return 0;
}

/*
 * 入队：把元素拷贝到队尾。
 * 参数：queue - 队列；item - 待入队元素（长度为 item_size）
 * 返回：0 成功；-1 参数非法或队列已满
 * 注意：队列满时直接失败，不会覆盖旧数据。
 */
int queue_push(queue_t *queue, const void *item)
{
    uint8_t *dst;

    if (queue == NULL || item == NULL) {
        return -1;
    }
    if (queue->count >= queue->capacity) {
        return -1;   /* 队列满 */
    }
    /* 定位队尾槽位：tail 以元素为单位，需乘以 item_size 得到字节偏移 */
    dst = queue->buffer + ((size_t)queue->tail * queue->item_size);
    memcpy(dst, item, queue->item_size);
    queue->tail = (queue->tail + 1) % queue->capacity;   /* 环形回绕 */
    queue->count++;
    return 0;
}

/*
 * 出队：把队首元素拷贝到 item 并前移队首。
 * 参数：queue - 队列；item - 输出缓冲（容量至少 item_size）
 * 返回：0 成功；-1 参数非法或队列为空
 */
int queue_pop(queue_t *queue, void *item)
{
    const uint8_t *src;

    if (queue == NULL || item == NULL) {
        return -1;
    }
    if (queue->count == 0) {
        return -1;   /* 队列空 */
    }
    src = queue->buffer + ((size_t)queue->head * queue->item_size);
    memcpy(item, src, queue->item_size);
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return 0;
}

/*
 * 查看队首元素但不移除。
 * 参数：queue - 队列；item - 输出缓冲（容量至少 item_size）
 * 返回：0 成功；-1 参数非法或队列为空
 */
int queue_peek(queue_t *queue, void *item)
{
    const uint8_t *src;

    if (queue == NULL || item == NULL) {
        return -1;
    }
    if (queue->count == 0) {
        return -1;
    }
    src = queue->buffer + ((size_t)queue->head * queue->item_size);
    memcpy(item, src, queue->item_size);
    return 0;
}

/*
 * 判断队列是否为空。
 * 参数：queue - 队列
 * 返回：true 为空（queue 为 NULL 也视为空）；false 非空
 */
bool queue_empty(const queue_t *queue)
{
    return (queue == NULL) || (queue->count == 0);
}

/*
 * 判断队列是否已满。
 * 参数：queue - 队列
 * 返回：true 已满；false 未满或 queue 为 NULL
 */
bool queue_full(const queue_t *queue)
{
    return (queue != NULL) && (queue->count >= queue->capacity);
}

/*
 * 查询当前元素个数。
 * 参数：queue - 队列
 * 返回：元素个数；queue 为 NULL 时返回 0
 */
uint32_t queue_size(const queue_t *queue)
{
    return (queue == NULL) ? 0 : queue->count;
}

/*
 * 清空队列（只重置索引与计数，不清零缓冲区内容）。
 * 参数：queue - 队列，为 NULL 时直接返回
 * 返回：无
 */
void queue_clear(queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}
