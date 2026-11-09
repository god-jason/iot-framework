#ifndef IOT_WAIT_H
#define IOT_WAIT_H

/*
 * iot_wait.h - 阻塞与唤醒原语（iot_wait.c）
 *
 * 等待队列 iot_wait_queue_t 采用「位图 + 每优先级一条链表」：
 *   - 挂入：按挂入瞬间的优先级入链并置位 → O(1)；
 *   - 取最高优先级等待者：ctz(bitmap) → O(1)；
 *   - 摘除：用 wait_node.sort_key 记住挂入时的优先级，
 *     即使之后被优先级继承提升也能准确回到原链 → O(1)。
 *
 * 所有带阻塞能力的对象（信号量/互斥量/队列/事件组/通知）都内嵌本结构。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iot_wait_queue {
    uint32_t bitmap;                       /* bit i = 优先级 i 有等待者 */
    list_t   prio[IOT_MAX_PRIORITY];        /* 每优先级一条 FIFO 链表 */
};

/* 初始化等待队列（对象 init 时调用） */
void iot_wait_queue_init(iot_wait_queue_t *queue);

/* 挂入当前任务（wait_node）；调用者需已持有临界区 */
void iot_wait_queue_add(iot_wait_queue_t *queue, iot_task_t *task);

/* 摘除任务并维护位图；对未挂入的任务安全 */
void iot_wait_queue_remove(iot_wait_queue_t *queue, iot_task_t *task);

/* 取出优先级最高的等待者并摘链；空队列返回 NULL */
iot_task_t *iot_wait_queue_pop_highest(iot_wait_queue_t *queue);

/* 队列中最高优先级；空队列返回 0xFF */
uint8_t iot_wait_queue_top_priority(const iot_wait_queue_t *queue);

/* 任务是否正挂在指定等待队列上 */
bool iot_wait_queue_contains(const iot_wait_queue_t *queue,
                             const iot_task_t *task);

/* 等待队列是否为空（无任何等待者） */
bool iot_wait_queue_empty(const iot_wait_queue_t *queue);

/* 按位图从小到大遍历各非空优先级链（事件组广播唤醒用） */
uint8_t iot_wait_queue_first_prio(uint32_t bitmap);

/* ------------------------------ 阻塞 / 唤醒 ------------------------------ */

/*
 * 将当前任务挂到 wait_queue 并阻塞，然后触发调度。
 *   timeout : IOT_TIMEOUT_FOREVER 永久等待；>0 时挂入延时链表，到期返回 IOT_TIMEOUT
 * 中断上下文中调用会返回 IOT_FORBIDDEN_IN_ISR（并触发断言）。
 */
int iot_block_current(iot_wait_queue_t *wait_queue, uint32_t timeout);

/* 唤醒任务：摘除等待/延时节点，写入结果并加入就绪表 */
void iot_task_wake(iot_task_t *task, int result);

#ifdef __cplusplus
}
#endif

#endif /* IOT_WAIT_H */
