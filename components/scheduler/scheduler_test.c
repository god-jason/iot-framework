/* scheduler 单元测试：标准 crontab 解析、匹配、宏、去重与容量。 */
#include "test.h"
#include "scheduler.h"

#include <string.h>

static int g_fires;
static int g_user_hits;

static void sched_cb(void *user)
{
    g_fires++;
    if (user != NULL) {
        (*(int *)user)++;
    }
}

static datetime_t make_dt(uint16_t year, uint8_t month, uint8_t day,
                              uint8_t hour, uint8_t minute, uint8_t second,
                              uint8_t weekday)
{
    datetime_t dt;

    memset(&dt, 0, sizeof(dt));
    dt.year = year;
    dt.month = month;
    dt.day = day;
    dt.hour = hour;
    dt.minute = minute;
    dt.second = second;
    dt.weekday = weekday;
    return dt;
}

static void test_add_errors(void)
{
    uint32_t id = 123;

    tf_begin("scheduler/add 参数与字段数");
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add(NULL, sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("* * * * *", NULL, NULL, &id), -1);
    TF_CHECK_INT(id, 123);
    TF_CHECK_INT(scheduler_add("0 8 * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("0 8 * * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("   ", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("@bogus", sched_cb, NULL, &id), -1);
    tf_end();

    tf_begin("scheduler/add 字段越界与语法错误");
    TF_CHECK_INT(scheduler_add("60 * * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("* 24 * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("* * 0 * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("* * 32 * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("* * * 0 *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("* * * 13 *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("* * * * 8", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("a * * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("1- * * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("5-1 * * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("*/0 * * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("1/2 * * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("1,,2 * * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("1, * * * *", sched_cb, NULL, &id), -1);
    TF_CHECK_INT(scheduler_add("1-2-3 * * * *", sched_cb, NULL, &id), -1);
    tf_end();

    tf_begin("scheduler/add 成功返回递增 ID");
    TF_CHECK_INT(scheduler_init(), 0);
    id = 99;
    TF_CHECK_INT(scheduler_add(" 0  8 * * * ", sched_cb, NULL, &id), 0);
    TF_CHECK_INT(id, 0);
    TF_CHECK_INT(scheduler_add("0\t9 * * *", sched_cb, NULL, &id), 0);
    TF_CHECK_INT(id, 1);
    TF_CHECK_INT(scheduler_add("0 10 * * *", sched_cb, NULL, NULL), 0);
    tf_end();
}

static void test_minute_match(void)
{
    datetime_t now;

    g_fires = 0;
    g_user_hits = 0;

    tf_begin("scheduler/分钟匹配与同一分钟去重");
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("30 8 * * *", sched_cb, &g_user_hits, NULL),
                 0);

    now = make_dt(2024, 1, 1, 8, 29, 59, 1);
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(g_fires, 0);

    now = make_dt(2024, 1, 1, 8, 30, 0, 1);
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(g_fires, 1);
    TF_CHECK_INT(g_user_hits, 1);

    now = make_dt(2024, 1, 1, 8, 30, 45, 1);
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(g_fires, 1);

    now = make_dt(2024, 1, 1, 8, 31, 0, 1);
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(g_fires, 1);

    now = make_dt(2024, 1, 2, 8, 30, 0, 2);
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(g_fires, 2);
    tf_end();
}

static void test_field_syntax(void)
{
    datetime_t now;
    uint8_t minute;

    tf_begin("scheduler/步进 */15");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("*/15 * * * *", sched_cb, NULL, NULL), 0);
    for (minute = 0U; minute < 60U; minute++) {
        now = make_dt(2024, 1, 1, 0, minute, 0, 1);
        scheduler_process(&now);
    }
    TF_CHECK_INT(g_fires, 4);   /* 0/15/30/45 */
    tf_end();

    tf_begin("scheduler/范围步进 9-17/2");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("0 9-17/2 * * *", sched_cb, NULL, NULL),
                 0);
    for (minute = 0U; minute < 24U; minute++) {
        now = make_dt(2024, 1, 1, minute, 0, 0, 1);
        scheduler_process(&now);
    }
    TF_CHECK_INT(g_fires, 5);   /* 9/11/13/15/17 */
    tf_end();

    tf_begin("scheduler/列表 1,15 与月份范围 6-8");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("0 0 1,15 * *", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 1, 1, 0, 0, 0, 1);
    scheduler_process(&now);
    now = make_dt(2024, 1, 2, 0, 0, 0, 2);
    scheduler_process(&now);
    now = make_dt(2024, 1, 15, 0, 0, 0, 1);
    scheduler_process(&now);
    now = make_dt(2024, 1, 16, 0, 0, 0, 2);
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 2);

    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("0 0 * 6-8 *", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 5, 31, 0, 0, 0, 5);
    scheduler_process(&now);
    now = make_dt(2024, 6, 1, 0, 0, 0, 6);
    scheduler_process(&now);
    now = make_dt(2024, 7, 1, 0, 0, 0, 1);
    scheduler_process(&now);
    now = make_dt(2024, 8, 1, 0, 0, 0, 4);
    scheduler_process(&now);
    now = make_dt(2024, 9, 1, 0, 0, 0, 0);
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 3);
    tf_end();

    tf_begin("scheduler/周字段 1-5 与 7=周日");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("0 0 * * 1-5", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 1, 1, 0, 0, 0, 1);   /* 周一 */
    scheduler_process(&now);
    now = make_dt(2024, 1, 6, 0, 0, 0, 6);   /* 周六 */
    scheduler_process(&now);
    now = make_dt(2024, 1, 8, 0, 0, 0, 1);   /* 周一 */
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 2);

    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("0 0 * * 7", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 1, 7, 0, 0, 0, 0);   /* 周日 */
    scheduler_process(&now);
    now = make_dt(2024, 1, 8, 0, 0, 0, 1);   /* 周一 */
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 1);
    tf_end();
}

static void test_dom_dow_or(void)
{
    datetime_t now;

    tf_begin("scheduler/日与周同时限定时为或关系");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("0 0 13 * 5", sched_cb, NULL, NULL), 0);

    now = make_dt(2024, 9, 13, 0, 0, 0, 5);  /* 13 日且周五 */
    scheduler_process(&now);
    now = make_dt(2024, 9, 6, 0, 0, 0, 5);   /* 仅周五 */
    scheduler_process(&now);
    now = make_dt(2024, 9, 14, 0, 0, 0, 6);  /* 周六且非 13 日 */
    scheduler_process(&now);
    now = make_dt(2024, 10, 13, 0, 0, 0, 0); /* 仅 13 日 */
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 3);
    tf_end();

    tf_begin("scheduler/日或周为 * 时为与关系");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("0 0 13 * *", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 9, 13, 0, 0, 0, 5);
    scheduler_process(&now);
    now = make_dt(2024, 9, 6, 0, 0, 0, 5);
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 1);
    tf_end();
}

static void test_macros(void)
{
    datetime_t now;

    tf_begin("scheduler/宏 @hourly @daily");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("@hourly", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 1, 1, 1, 0, 0, 1);
    scheduler_process(&now);
    now = make_dt(2024, 1, 1, 1, 1, 0, 1);
    scheduler_process(&now);
    now = make_dt(2024, 1, 1, 2, 0, 0, 1);
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 2);

    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("@daily", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 1, 1, 0, 0, 30, 1);
    scheduler_process(&now);
    now = make_dt(2024, 1, 1, 0, 1, 0, 1);
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 1);
    tf_end();

    tf_begin("scheduler/宏 @weekly @monthly @yearly");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("@weekly", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 1, 7, 0, 0, 0, 0);   /* 周日 */
    scheduler_process(&now);
    now = make_dt(2024, 1, 8, 0, 0, 0, 1);   /* 周一 */
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 1);

    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("@monthly", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 2, 1, 0, 0, 0, 4);
    scheduler_process(&now);
    now = make_dt(2024, 2, 2, 0, 0, 0, 5);
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 1);

    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("@yearly", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 1, 1, 0, 0, 0, 1);
    scheduler_process(&now);
    now = make_dt(2024, 1, 2, 0, 0, 0, 2);
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 1);
    tf_end();

    tf_begin("scheduler/宏 @reboot");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("@reboot", sched_cb, NULL, NULL), 0);
    now = make_dt(2024, 1, 1, 3, 7, 9, 1);
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 1);
    now = make_dt(2024, 1, 1, 3, 7, 10, 1);
    scheduler_process(&now);
    TF_CHECK_INT(g_fires, 1);
    tf_end();
}

static void test_remove_capacity(void)
{
    datetime_t now;
    uint32_t id;
    int i;

    tf_begin("scheduler/remove 合法与非法 ID");
    g_fires = 0;
    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_remove(0), -1);
    TF_CHECK_INT(scheduler_remove(8), -1);
    TF_CHECK_INT(scheduler_remove(99), -1);

    TF_CHECK_INT(scheduler_add("10 0 * * *", sched_cb, NULL, &id), 0);
    TF_CHECK_INT(scheduler_remove(id), 0);
    TF_CHECK_INT(scheduler_remove(id), -1);
    now = make_dt(2024, 1, 1, 0, 10, 0, 1);
    TF_CHECK_INT(scheduler_process(&now), 0);
    TF_CHECK_INT(g_fires, 0);
    tf_end();

    tf_begin("scheduler/容量上限 8 与 init 复位");
    TF_CHECK_INT(scheduler_init(), 0);
    for (i = 0; i < 8; i++) {
        char expr[16];

        snprintf(expr, sizeof(expr), "%d 0 * * *", i);
        TF_CHECK_INT(scheduler_add(expr, sched_cb, NULL, &id), 0);
        TF_CHECK_INT(id, i);
    }
    TF_CHECK_INT(scheduler_add("9 0 * * *", sched_cb, NULL, &id), -1);

    TF_CHECK_INT(scheduler_init(), 0);
    TF_CHECK_INT(scheduler_add("9 0 * * *", sched_cb, NULL, &id), 0);
    TF_CHECK_INT(id, 0);
    tf_end();

    tf_begin("scheduler/process 非法参数");
    TF_CHECK_INT(scheduler_process(NULL), -1);
    now = make_dt(2024, 13, 1, 0, 0, 0, 1);
    TF_CHECK_INT(scheduler_process(&now), -1);
    now = make_dt(2024, 1, 32, 0, 0, 0, 1);
    TF_CHECK_INT(scheduler_process(&now), -1);
    tf_end();
}

void ut_scheduler(void)
{
    test_add_errors();
    test_minute_match();
    test_field_syntax();
    test_dom_dow_or();
    test_macros();
    test_remove_capacity();
}
