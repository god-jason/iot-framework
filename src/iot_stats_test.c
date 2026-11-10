/*
 * iot_stats_test.c - 运行时统计测试（对应 iot_stats.c）
 *
 * 覆盖：内核切换计数、tick 计数、任务运行统计、栈水位、重置。
 */

#include "test.h"
#include "iot.h"

void iot_test_stats(void)
{
    iot_stats_t stats;
    iot_task_stats_t task_stats;

    tf_begin("iot/统计-内核级");

    TF_CHECK_INT(iot_stats_get(NULL), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_stats_get(&stats), IOT_OK);
    TF_CHECK(stats.ctx_switches > 0U);
    TF_CHECK(stats.tick_count > 0U);
    TF_CHECK_INT(stats.tick_count, (int)iot_tick_get());

    tf_end();

    tf_begin("iot/统计-任务级与栈水位");

    TF_CHECK_INT(iot_task_get_stats(NULL, &task_stats), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_task_get_stats(iot_current(), NULL), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_task_get_stats(iot_current(), &task_stats), IOT_OK);
    TF_CHECK(task_stats.stack_free_bytes > 0U);
    TF_CHECK(task_stats.stack_free_bytes <= 8192U);
    TF_CHECK(task_stats.switches > 0U);

    /* CPU 占用率：千分比，范围 0..1000 */
    TF_CHECK_INT(iot_task_cpu_permille(NULL), 0);
    TF_CHECK(iot_task_cpu_permille(iot_current()) <= 1000U);

    tf_end();

    tf_begin("iot/统计-重置");

    iot_stats_reset();
    TF_CHECK_INT(iot_stats_get(&stats), IOT_OK);
    TF_CHECK_INT(stats.ctx_switches, 0);

    TF_CHECK_INT(iot_task_get_stats(iot_current(), &task_stats), IOT_OK);
    TF_CHECK_INT(task_stats.run_ticks, 0);
    TF_CHECK_INT(task_stats.switches, 0);
    TF_CHECK_INT(iot_task_cpu_permille(iot_current()), 0);

    tf_end();
}
