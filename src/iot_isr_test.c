/*
 * iot_isr_test.c - 中断上下文接口测试（对应 iot_isr.c）
 *
 * 覆盖：FromISR 只唤醒不阻塞、need_yield 与抢占判定一致、
 *       队列满时立即返回、事件/通知的 ISR 版本。
 */

#include "test.h"
#include "iot.h"

static void test_from_isr(void)
{
    iot_sem_t sem;
    iot_queue_t queue;
    uint8_t qbuf[4U * sizeof(uint32_t)];
    uint32_t msg = 0x1234U;
    uint32_t out = 0U;
    bool need_yield = true;
    uint32_t i;

    tf_begin("iot/FromISR接口");

    /* PC 模拟端全部逻辑都在任务上下文 */
    TF_CHECK(!iot_in_isr());

    TF_CHECK_INT(iot_sem_init(&sem, 0U), IOT_OK);
    TF_CHECK_INT(iot_sem_give_from_isr(&sem, &need_yield), IOT_OK);
    TF_CHECK(need_yield == iot_sched_preempt_needed());
    TF_CHECK_INT(iot_sem_take(&sem, IOT_TIMEOUT_NONE), IOT_OK);

    TF_CHECK_INT(iot_queue_init(&queue, qbuf,
                                (uint32_t)sizeof(uint32_t), 4U), IOT_OK);
    TF_CHECK_INT(iot_queue_send_from_isr(&queue, &msg, NULL), IOT_OK);
    TF_CHECK_INT(iot_queue_recv(&queue, &out, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(out, 0x1234);

    /* 填满后 FromISR 必须立即返回，绝不阻塞 */
    for (i = 0U; i < 4U; i++) {
        TF_CHECK_INT(iot_queue_send_from_isr(&queue, &msg, NULL), IOT_OK);
    }
    TF_CHECK_INT(iot_queue_send_from_isr(&queue, &msg, NULL), IOT_TIMEOUT);

#if IOT_USE_EVENT
    {
        iot_event_t event;
        uint32_t flags = 0U;

        TF_CHECK_INT(iot_event_init(&event), IOT_OK);
        TF_CHECK_INT(iot_event_set_from_isr(&event, 0x5U, NULL), IOT_OK);
        TF_CHECK_INT(iot_event_get(&event, &flags), IOT_OK);
        TF_CHECK_INT(flags, 0x5);
    }
#endif

#if IOT_USE_NOTIFY
    {
        uint32_t value = 0U;

        TF_CHECK_INT(iot_task_notify_from_isr(iot_current(), 0x77U, NULL), IOT_OK);
        TF_CHECK_INT(iot_task_wait_notify(iot_current(),
                                          IOT_TIMEOUT_NONE, &value), IOT_OK);
        TF_CHECK_INT(value, 0x77);
    }
#endif

    tf_end();
}

void iot_test_isr(void)
{
    test_from_isr();
}
