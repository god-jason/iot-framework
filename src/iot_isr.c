/*
 * iot_isr.c - 中断上下文专用接口
 *
 * 所有 FromISR 入口都不阻塞：
 *   - give / set / notify 本身无阻塞路径；
 *   - queue_send 强制 timeout = IOT_TIMEOUT_NONE，满则立即返回。
 * 它们复用任务上下文实现（其临界区为 PRIMASK 保存/恢复，ISR 内安全），
 * 唤醒高优先级任务时由 iot_task_wake() 自动请求 PendSV。
 */

#include "iot.h"
#include "iot_port.h"
#include "iot_isr.h"

bool iot_in_isr(void)
{
    return iot_port_in_isr();
}

void iot_yield_from_isr(void)
{
    iot_port_request_switch();
}

static int iot_isr_finish(int rc, bool *need_yield)
{
    if (need_yield != NULL) {
        *need_yield = iot_sched_preempt_needed();
    }
    return rc;
}

int iot_sem_give_from_isr(iot_sem_t *sem, bool *need_yield)
{
    return iot_isr_finish(iot_sem_give(sem), need_yield);
}

int iot_queue_send_from_isr(iot_queue_t *queue, const void *item,
                            bool *need_yield)
{
    return iot_isr_finish(iot_queue_send(queue, item, IOT_TIMEOUT_NONE),
                          need_yield);
}

#if IOT_USE_EVENT
int iot_event_set_from_isr(iot_event_t *event, uint32_t bits,
                           bool *need_yield)
{
    return iot_isr_finish(iot_event_set(event, bits), need_yield);
}
#endif

#if IOT_USE_NOTIFY
int iot_task_notify_from_isr(iot_task_t *task, uint32_t value,
                             bool *need_yield)
{
    return iot_isr_finish(iot_task_notify(task, value), need_yield);
}
#endif
