#ifndef IOT_WAIT_H
#define IOT_WAIT_H

/*
 * wait.h - 阻塞与唤醒原语（wait.c）
 *
 * 信号量/互斥量/队列统一通过 iot_block_current() 挂入对象等待链表，
 * 条件满足时通过 iot_task_wake() 唤醒。
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 将当前任务挂到 wait_list 并阻塞，然后触发调度。
 *   wait_list : 等待队列（wait_node）
 *   timeout   : IOT_TIMEOUT_FOREVER 永久等待；>0 时挂入延时链表，到期返回 IOT_TIMEOUT
 * 返回：被唤醒时写入的结果码（IOT_OK / IOT_TIMEOUT）。
 */
int iot_block_current(list_t *wait_list, uint32_t timeout);

/* 唤醒任务：摘除等待/延时节点，写入结果并加入就绪表 */
void iot_task_wake(iot_task_t *task, int result);

/* 从等待队列取出优先级最高的任务（摘除 wait_node）；无则返回 NULL */
iot_task_t *iot_wait_pop_highest(list_t *list);

#ifdef __cplusplus
}
#endif

#endif /* IOT_WAIT_H */
