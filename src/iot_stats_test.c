/*
 * iot_stats_test.c - 运行时统计测试（对应 iot_stats.c）
 *
 * 覆盖：上下文切换次数、tick 计数、单任务统计与栈水位线。
 */

#include "test.h"
#include "iot.h"

static void test_stats(void)
{
    iot_stats_t stats;
    iot_task_stats_t task_stats;

    tf_begin("iot/运行时统计");

    TF_CHECK_INT(iot_stats_get(&stats), IOT_OK);
    TF_CHECK(stats.ctx_switches > 0U);
    TF_CHECK(stats.tick_count > 0U);

    TF_CHECK_INT(iot_task_get_stats(iot_current(), &task_stats), IOT_OK);
    TF_CHECK(task_stats.stack_free_bytes > 0U);

    iot_stats_reset();
    TF_CHECK_INT(iot_stats_get(&stats), IOT_OK);
    TF_CHECK_INT(stats.ctx_switches, 0);

    tf_end();
}

/* 整栈魔数填充后，栈水位线应反映真实可用空间 */
static void test_stack_watermark(void)
{
    iot_task_stats_t stats;

    tf_begin("iot/栈水位线");

    TF_CHECK_INT(iot_task_get_stats(iot_current(), &stats), IOT_OK);
    TF_CHECK(stats.stack_free_bytes >= 4096U);

    iot_stats_reset();
    TF_CHECK_INT(iot_task_get_stats(iot_current(), &stats), IOT_OK);
    TF_CHECK_INT(stats.run_ticks, 0);
    TF_CHECK_INT(stats.switches, 0);

    tf_end();
}

void iot_test_stats(void)
{
    test_stats();
    test_stack_watermark();
}
