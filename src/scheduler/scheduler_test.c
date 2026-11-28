/*
 * scheduler_test.c - crontab 调度器测试（对应 scheduler.c）
 *
 * 覆盖：表达式解析（通配/单值/范围/列表/步进/宏）、注册与删除、
 *       到期触发与同分钟去重、非法表达式。
 */

#include "test.h"
#include "scheduler.h"

static int s_hits;
static uint32_t s_last_id;

static void on_tick(void *user)
{
    (void)user;
    s_hits++;
}

void ut_scheduler(void)
{
    datetime_t now;
    uint32_t id = 0;

    tf_begin("scheduler/注册与表达式校验");

    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add(NULL, on_tick, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("0 0 * * *", NULL, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("bad expr", on_tick, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("61 * * * *", on_tick, NULL, &id), -1);

    TF_CHECK_INT(scheduler_add("*/5 * * * *", on_tick, NULL, &id), 0);
    TF_CHECK_INT(id, 0);                       /* 编号从 0 开始 */
    TF_CHECK_INT(scheduler_add("@daily", on_tick, NULL, &s_last_id), 0);
    TF_CHECK_INT(scheduler_add("0 12 1-5 1,6,12 *", on_tick, NULL, NULL), 0);

    tf_end();

    tf_begin("scheduler/触发与同分钟去重");

    s_hits = 0;
    memset(&now, 0, sizeof(now));
    now.year = 2024U;
    now.month = 6U;
    now.day = 1U;
    now.hour = 10U;
    now.minute = 0U;          /* 匹配 */

    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(s_hits, 1);

    /* 同一分钟重复调用不重复触发 */
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(s_hits, 1);

    now.minute = 1U;          /* 不匹配 */
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(s_hits, 1);

    now.minute = 5U;          /* 再次匹配 */
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(s_hits, 2);

    tf_end();

    tf_begin("scheduler/删除与非法参数");

    TF_CHECK_INT(scheduler_remove(0U), 0);
    TF_CHECK_INT(scheduler_remove(0U), -1);

    s_hits = 0;
    now.minute = 5U;
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(s_hits, 0);          /* 剩余任务在该时刻均不匹配 */

    TF_CHECK_INT(scheduler_process(NULL), -1);

    tf_end();
}
