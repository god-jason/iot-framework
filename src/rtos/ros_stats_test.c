/*
 * ros_stats_test.c - 运行时统计测试（对应 ros_stats.c）
 *
 * 覆盖：内核切换计数、tick 计数、任务运行统计、栈水位、重置。
 */

#include "test.h"
#include "ros.h"
#include "common.h"

void ros_test_stats(void)
{
    ros_stats_t stats;
    ros_task_stats_t task_stats;

    tf_begin("ros/统计-内核级");

    TF_CHECK_INT(ros_stats_get(NULL), ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_stats_get(&stats), ROS_OK);
    TF_CHECK(stats.ctx_switches > 0U);
    TF_CHECK(stats.tick_count > 0U);
    TF_CHECK_INT(stats.tick_count, (int)ros_tick_get());

    tf_end();

    tf_begin("ros/统计-任务级与栈水位");

    TF_CHECK_INT(ros_task_get_stats(NULL, &task_stats), ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_task_get_stats(ros_current(), NULL), ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_task_get_stats(ros_current(), &task_stats), ROS_OK);
    TF_CHECK(task_stats.stack_free_bytes > 0U);
    TF_CHECK(task_stats.stack_free_bytes <= 8192U);
    TF_CHECK(task_stats.switches > 0U);

    /* CPU 占用率：千分比，范围 0..1000 */
    TF_CHECK_INT(ros_task_cpu_permille(NULL), 0);
    TF_CHECK(ros_task_cpu_permille(ros_current()) <= 1000U);

    tf_end();

    tf_begin("ros/统计-重置");

    ros_stats_reset();
    TF_CHECK_INT(ros_stats_get(&stats), ROS_OK);
    TF_CHECK_INT(stats.ctx_switches, 0);

    TF_CHECK_INT(ros_task_get_stats(ros_current(), &task_stats), ROS_OK);
    TF_CHECK_INT(task_stats.run_ticks, 0);
    TF_CHECK_INT(task_stats.switches, 0);
    TF_CHECK_INT(ros_task_cpu_permille(ros_current()), 0);

    tf_end();
}
