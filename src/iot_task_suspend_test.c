/*
 * iot_task_suspend_test.c - 任务挂起/恢复回归测试
 *
 * 只验证状态机与嵌套计数（不依赖调度时序），保持结果确定性。
 * 被挂起的任务不会进入就绪表，因此不会意外参与调度。
 */

#include "test.h"
#include "iot.h"

static iot_task_t s_worker;
static uint8_t    s_stack[2048];

static void worker_entry(void *arg)
{
    (void)arg;
    for (;;) {
        iot_sleep_ms(10U);
    }
}

void iot_test_suspend(void)
{
    tf_begin("iot/task suspend-resume");

    TF_CHECK_INT(iot_task_create(&s_worker, "sus_worker", 6U,
                                 worker_entry, NULL,
                                 s_stack, sizeof(s_stack)), IOT_OK);
    TF_CHECK(!iot_task_is_suspended(&s_worker));
    TF_CHECK_INT(s_worker.state, IOT_TASK_READY);

    /* 1. 挂起 READY 任务 */
    TF_CHECK_INT(iot_task_suspend(&s_worker), IOT_OK);
    TF_CHECK(iot_task_is_suspended(&s_worker));
    TF_CHECK_INT(s_worker.state, IOT_TASK_SUSPENDED);
    TF_CHECK_INT(s_worker.suspend_count, 1);

    /* 2. 挂起态不能被就绪/唤醒路径意外放回 */
    iot_make_ready(&s_worker);
    TF_CHECK_INT(s_worker.state, IOT_TASK_SUSPENDED);

    /* 3. 嵌套挂起：两次 suspend 需要两次 resume */
    TF_CHECK_INT(iot_task_suspend(&s_worker), IOT_OK);
    TF_CHECK_INT(s_worker.suspend_count, 2);
    TF_CHECK_INT(iot_task_resume(&s_worker), IOT_OK);
    TF_CHECK(iot_task_is_suspended(&s_worker));
    TF_CHECK_INT(s_worker.suspend_count, 1);

    TF_CHECK_INT(iot_task_resume(&s_worker), IOT_OK);
    TF_CHECK(!iot_task_is_suspended(&s_worker));
    TF_CHECK_INT(s_worker.state, IOT_TASK_READY);
    TF_CHECK_INT(s_worker.suspend_count, 0);

    /* 4. 边界：未挂起任务不能再 resume；空指针参数 */
    TF_CHECK_INT(iot_task_resume(&s_worker), IOT_NOT_READY);
    TF_CHECK_INT(iot_task_suspend(NULL), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_task_resume(NULL), IOT_INVALID_PARAM);
    TF_CHECK(!iot_task_is_suspended(NULL));

    /* 5. 挂起 BLOCKED 任务应被保守拒绝 */
    {
        iot_sem_t sem;
        TF_CHECK_INT(iot_sem_init(&sem, 0U), IOT_OK);
        /* 直接置状态模拟阻塞中的任务，避免真的切走当前任务 */
        s_worker.state = IOT_TASK_BLOCKED;
        TF_CHECK_INT(iot_task_suspend(&s_worker), IOT_BUSY);
        s_worker.state = IOT_TASK_READY;
    }

    /* 收尾：从就绪表摘除，避免该静态任务被后续套件调度到 */
    s_worker.state = IOT_TASK_READY;
    TF_CHECK_INT(iot_sched_remove(&s_worker), IOT_OK);
    TF_CHECK_INT(s_worker.state, IOT_TASK_TERMINATED);

    tf_end();
}
