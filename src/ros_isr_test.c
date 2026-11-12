/*
 * ros_isr_test.c - 中断上下文接口测试（对应 ros_isr.c）
 *
 * 覆盖：上下文判定、FromISR 只唤醒不阻塞、need_yield 与抢占判定一致、
 *       队列满时立即返回、事件/通知的 ISR 版本。
 */

#include "test.h"
#include "ros.h"

void ros_test_isr(void)
{
    ros_sem_t sem;
    ros_queue_t queue;
    uint8_t qbuf[4U * sizeof(uint32_t)];
    uint32_t msg = 0x1234U;
    uint32_t out = 0U;
    bool need_yield = true;
    uint32_t i;

    tf_begin("ros/中断-上下文与信号量");

    /* PC 模拟端全部逻辑都在任务上下文 */
    TF_CHECK(!ros_in_isr());

    TF_CHECK_INT(ros_sem_init(&sem, 0U), ROS_OK);
    TF_CHECK_INT(ros_sem_give_from_isr(&sem, &need_yield), ROS_OK);
    TF_CHECK(need_yield == ros_sched_preempt_needed());
    TF_CHECK_INT(ros_sem_take(&sem, ROS_TIMEOUT_NONE), ROS_OK);

    tf_end();

    tf_begin("ros/中断-队列非阻塞发送");

    TF_CHECK_INT(ros_queue_init(&queue, qbuf, sizeof(uint32_t), 4U), ROS_OK);
    TF_CHECK_INT(ros_queue_send_from_isr(&queue, &msg, NULL), ROS_OK);
    TF_CHECK_INT(ros_queue_recv(&queue, &out, ROS_TIMEOUT_NONE), ROS_OK);
    TF_CHECK_INT(out, 0x1234);

    for (i = 0U; i < 4U; i++) {
        TF_CHECK_INT(ros_queue_send_from_isr(&queue, &msg, NULL), ROS_OK);
    }
    TF_CHECK_INT(ros_queue_send_from_isr(&queue, &msg, NULL), ROS_TIMEOUT);

    tf_end();

#if ROS_USE_EVENT
    tf_begin("ros/中断-事件与通知");

    {
        ros_event_t event;
        uint32_t flags = 0U;

        TF_CHECK_INT(ros_event_init(&event), ROS_OK);
        TF_CHECK_INT(ros_event_set_from_isr(&event, 0x5U, NULL), ROS_OK);
        TF_CHECK_INT(ros_event_get(&event, &flags), ROS_OK);
        TF_CHECK_INT(flags, 0x5);
    }

#if ROS_USE_NOTIFY
    {
        uint32_t value = 0U;

        TF_CHECK_INT(ros_task_notify_from_isr(ros_current(), 0x77U, NULL),
                     ROS_OK);
        TF_CHECK_INT(ros_task_wait_notify(ros_current(), ROS_TIMEOUT_NONE,
                                          &value), ROS_OK);
        TF_CHECK_INT(value, 0x77);
    }
#endif

    tf_end();
#endif

    tf_begin("ros/中断-显式让出请求");

    /* PC 模拟端为空实现，只验证可调用且不崩溃 */
    ros_yield_from_isr();
    TF_CHECK(1);

    tf_end();
}
