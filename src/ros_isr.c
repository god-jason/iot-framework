/*
 * ros_isr.c - 中断上下文专用接口
 *
 * 所有 FromISR 入口都不阻塞：
 *   - give / set / notify 本身无阻塞路径；
 *   - queue_send 强制 timeout = ROS_TIMEOUT_NONE，满则立即返回。
 * 它们复用任务上下文实现（其临界区为 PRIMASK 保存/恢复，ISR 内安全），
 * 唤醒高优先级任务时由 ros_task_wake() 自动请求 PendSV。
 */

#include "ros.h"
#include "ros_port.h"
#include "ros_isr.h"

/* 返回当前是否处于中断/异常上下文。 */
bool ros_in_isr(void)
{
    return ros_port_in_isr();
}

/* 显式请求一次重调度（MCU 端置位 PendSV，PC 端为空实现）。 */
void ros_yield_from_isr(void)
{
    ROS_TRACE_OBJ(ROS_EVT_FROM_ISR, ROS_EVT_PREEMPT, 0U);
    ros_port_request_switch();
}

/* FromISR 统一收尾：按需回传 need_yield，并原样返回操作结果。 */
static int ros_isr_finish(int rc, bool *need_yield)
{
    if (need_yield != NULL) {
        *need_yield = ros_sched_preempt_needed();
    }
    return rc;
}

/* 中断中释放信号量（非阻塞，必要时唤醒等待者）。 */
int ros_sem_give_from_isr(ros_sem_t *sem, bool *need_yield)
{
    return ros_isr_finish(ros_sem_give(sem), need_yield);
}

/* 中断中发送定长消息：强制非阻塞，队列满立即返回 ROS_TIMEOUT。 */
int ros_queue_send_from_isr(ros_queue_t *queue, const void *item,
                            bool *need_yield)
{
    return ros_isr_finish(ros_queue_send(queue, item, ROS_TIMEOUT_NONE),
                          need_yield);
}

/* 中断中接收定长消息：强制非阻塞，队列空立即返回 ROS_TIMEOUT。 */
int ros_queue_recv_from_isr(ros_queue_t *queue, void *item, bool *need_yield)
{
    return ros_isr_finish(ros_queue_recv(queue, item, ROS_TIMEOUT_NONE),
                          need_yield);
}

#if ROS_USE_SBUF
/* 中断中写入流式缓冲：非阻塞，只写当前可用空间并返回实际字节数。 */
int ros_sbuf_write_from_isr(ros_sbuf_t *sb, const void *data, uint32_t len,
                            bool *need_yield)
{
    return ros_isr_finish(ros_sbuf_write(sb, data, len, ROS_TIMEOUT_NONE),
                          need_yield);
}

/* 中断中读取流式缓冲：非阻塞，返回当前可用字节数。 */
int ros_sbuf_read_from_isr(ros_sbuf_t *sb, void *data, uint32_t len,
                           bool *need_yield)
{
    return ros_isr_finish(ros_sbuf_read(sb, data, len, ROS_TIMEOUT_NONE),
                          need_yield);
}
#endif

#if ROS_USE_MSGQ
/* 中断中发送变长消息：空间不足立即返回 ROS_TIMEOUT。 */
int ros_msgq_send_from_isr(ros_msgq_t *mq, const void *msg, uint32_t len,
                           bool *need_yield)
{
    return ros_isr_finish(ros_msgq_send(mq, msg, len, ROS_TIMEOUT_NONE),
                          need_yield);
}

/* 中断中接收变长消息：无消息或缓冲不足立即返回。 */
int ros_msgq_recv_from_isr(ros_msgq_t *mq, void *msg, uint32_t max_len,
                           uint32_t *out_len, bool *need_yield)
{
    return ros_isr_finish(ros_msgq_recv(mq, msg, max_len, out_len,
                                        ROS_TIMEOUT_NONE),
                          need_yield);
}
#endif

#if ROS_USE_EVENT
/* 中断中置位事件并唤醒满足条件的等待者。 */
int ros_event_set_from_isr(ros_event_t *event, uint32_t bits,
                           bool *need_yield)
{
    return ros_isr_finish(ros_event_set(event, bits), need_yield);
}
#endif

#if ROS_USE_NOTIFY
/* 中断中向任务发送通知。 */
int ros_task_notify_from_isr(ros_task_t *task, uint32_t value,
                             bool *need_yield)
{
    return ros_isr_finish(ros_task_notify(task, value), need_yield);
}
#endif
