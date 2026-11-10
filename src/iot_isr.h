#ifndef IOT_ISR_H
#define IOT_ISR_H

/*
 * iot_isr.h - 中断上下文专用接口（iot_isr.c）
 *
 * STM32 端软件定时器回调、SysTick 以及外设 ISR 都运行在异常上下文，
 * 不能调用任何可能阻塞的 API（*_take / *_lock / *_send|recv 带 timeout、
 * sleep、event_wait、wait_notify）。内核会在这些入口处断言并返回
 * IOT_FORBIDDEN_IN_ISR。
 *
 * 本头文件提供四类「只唤醒、不阻塞」的 FromISR 入口，语义与 FreeRTOS 的
 * xSemaphoreGiveFromISR / xQueueSendFromISR 等对齐：
 *   need_yield 可空；非空时写入「是否存在更高优先级任务就绪」。
 * 内核在 iot_task_wake() 中已自动请求一次抢占（置位 PendSV），
 * 因此调用方通常无需再调 iot_yield_from_isr()；该接口保留给需要
 * 显式控制在 ISR 尾部统一 yield 的工程。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 当前是否处于中断/异常上下文 */
bool iot_in_isr(void);

/* 显式请求一次重新调度（MCU 端置位 PendSV） */
void iot_yield_from_isr(void);

/* 抢占需求判定 iot_sched_preempt_needed() 声明在 iot_sched.h */

/* 中断中释放信号量 */
int iot_sem_give_from_isr(iot_sem_t *sem, bool *need_yield);

/* 中断中发送消息；队列满时立即返回 IOT_TIMEOUT，绝不阻塞 */
int iot_queue_send_from_isr(iot_queue_t *queue, const void *item,
                            bool *need_yield);

/* 中断中接收消息；队列空时立即返回 IOT_TIMEOUT，绝不阻塞 */
int iot_queue_recv_from_isr(iot_queue_t *queue, void *item,
                            bool *need_yield);

#if IOT_USE_SBUF
/* 中断中写入流式缓冲；只写当前可用空间，返回实际写入字节数 */
int iot_sbuf_write_from_isr(iot_sbuf_t *sb, const void *data, uint32_t len,
                            bool *need_yield);

/* 中断中读取流式缓冲；返回实际读取字节数 */
int iot_sbuf_read_from_isr(iot_sbuf_t *sb, void *data, uint32_t len,
                           bool *need_yield);
#endif

#if IOT_USE_MSGQ
/* 中断中发送变长消息；空间不足立即返回 IOT_TIMEOUT */
int iot_msgq_send_from_isr(iot_msgq_t *mq, const void *msg, uint32_t len,
                           bool *need_yield);

/* 中断中接收变长消息；无消息立即返回 IOT_TIMEOUT */
int iot_msgq_recv_from_isr(iot_msgq_t *mq, void *msg, uint32_t max_len,
                           uint32_t *out_len, bool *need_yield);
#endif

#if IOT_USE_EVENT
/* 中断中置位事件 */
int iot_event_set_from_isr(iot_event_t *event, uint32_t bits,
                           bool *need_yield);
#endif

#if IOT_USE_NOTIFY
/* 中断中发送任务通知 */
int iot_task_notify_from_isr(iot_task_t *task, uint32_t value,
                             bool *need_yield);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IOT_ISR_H */
