/*
 * iot_sched_test.c - 调度器测试（对应 iot_sched.c）
 *
 * 覆盖：抢占需求查询（当前为最高优先级任务时应无抢占）。
 */

#include "test.h"
#include "iot.h"

static void test_preempt_query(void)
{
    tf_begin("iot/抢占判定");

    /* driver 是最高优先级 0，不应有更高优先级任务就绪 */
    TF_CHECK(!iot_sched_preempt_needed());

    tf_end();
}

void iot_test_sched(void)
{
    test_preempt_query();
}
