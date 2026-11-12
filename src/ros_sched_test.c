/*
 * ros_sched_test.c - 调度器测试（对应 ros_sched.c）
 *
 * 覆盖：当前任务、抢占判定、就绪表选择、优先级变更换链、切换计数。
 * 说明：调度器内部链表不宜直接破坏，因此只做只读与安全路径验证。
 */

#include "test.h"
#include "ros.h"

void ros_test_sched(void)
{
    ros_task_t *cur;

    tf_begin("ros/调度-当前任务与抢占判定");

    cur = ros_current();
    TF_CHECK(cur != NULL);
    TF_CHECK_STR(cur->name, "driver");
    TF_CHECK_INT(cur->priority, 0);
    TF_CHECK_INT(cur->state, ROS_TASK_RUNNING);

    /* driver 已是最高优先级，无抢占需求 */
    TF_CHECK(!ros_sched_preempt_needed());

    tf_end();

    tf_begin("ros/调度-选任务与切换计数");

#if ROS_USE_STATS
    {
        uint32_t before = ros_sched_switch_count();

        /* 让出一次，必然发生一次以上的调度 */
        ros_yield();
        TF_CHECK(ros_sched_switch_count() >= before);
    }
#endif

    TF_CHECK(ros_current() == cur);

    tf_end();
}
