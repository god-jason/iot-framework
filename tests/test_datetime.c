/* 时间日期与定时任务测试。 */
#include "test_framework.h"
#include "iot_datetime.h"
#include "iot_cron.h"

#include <string.h>

static void test_calendar(void)
{
    tf_begin("datetime/闰年与月份天数");
    TF_CHECK(iot_datetime_is_leap_year(2000));
    TF_CHECK(!iot_datetime_is_leap_year(1900));
    TF_CHECK(iot_datetime_is_leap_year(2024));
    TF_CHECK(!iot_datetime_is_leap_year(2023));
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 2), 29);
    TF_CHECK_INT(iot_datetime_days_in_month(2023, 2), 28);
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 4), 30);
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 12), 31);
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 0), 0);
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 13), 0);
    tf_end();

    tf_begin("datetime/合法性判断");
    {
        iot_datetime_t dt;
        memset(&dt, 0, sizeof(dt));
        dt.year = 2024; dt.month = 2; dt.day = 29;
        TF_CHECK(iot_datetime_is_valid(&dt));
        dt.day = 30;
        TF_CHECK(!iot_datetime_is_valid(&dt));

        memset(&dt, 0, sizeof(dt));
        dt.year = 2023; dt.month = 2; dt.day = 29;
        TF_CHECK(!iot_datetime_is_valid(&dt));

        memset(&dt, 0, sizeof(dt));
        dt.year = 2024; dt.month = 13; dt.day = 1;
        TF_CHECK(!iot_datetime_is_valid(&dt));
    }
    tf_end();

    tf_begin("datetime/星期");
    TF_CHECK_INT(iot_datetime_weekday(1970, 1, 1), 4);   /* 周四 */
    TF_CHECK_INT(iot_datetime_weekday(2024, 1, 1), 1);   /* 周一 */
    tf_end();
}

static void test_timestamp(void)
{
    iot_datetime_t dt;
    int64_t ts = 0;

    tf_begin("datetime/时间戳换算");
    memset(&dt, 0, sizeof(dt));
    dt.year = 1970; dt.month = 1; dt.day = 1;
    TF_CHECK_INT(iot_datetime_to_timestamp(&dt, &ts), IOT_OK);
    TF_CHECK_INT(ts, 0);

    memset(&dt, 0, sizeof(dt));
    dt.year = 2000; dt.month = 1; dt.day = 1;
    TF_CHECK_INT(iot_datetime_to_timestamp(&dt, &ts), IOT_OK);
    TF_CHECK_INT(ts, 946684800);

    memset(&dt, 0, sizeof(dt));
    dt.year = 2024; dt.month = 1; dt.day = 1;
    TF_CHECK_INT(iot_datetime_to_timestamp(&dt, &ts), IOT_OK);
    TF_CHECK_INT(ts, 1704067200);
    tf_end();

    tf_begin("datetime/时间戳反解");
    TF_CHECK_INT(iot_timestamp_to_datetime(1704067200, &dt), IOT_OK);
    TF_CHECK_INT(dt.year, 2024);
    TF_CHECK_INT(dt.month, 1);
    TF_CHECK_INT(dt.day, 1);
    TF_CHECK_INT(dt.hour, 0);
    TF_CHECK_INT(dt.weekday, 1);

    TF_CHECK_INT(iot_timestamp_to_datetime(946684800, &dt), IOT_OK);
    TF_CHECK_INT(dt.year, 2000);
    TF_CHECK_INT(dt.month, 1);
    TF_CHECK_INT(dt.day, 1);
    tf_end();

    tf_begin("datetime/比较与增减");
    {
        iot_datetime_t a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        a.year = 2023; a.month = 12; a.day = 31;
        a.hour = 23; a.minute = 59; a.second = 59;
        b = a;
        TF_CHECK_INT(iot_datetime_compare(&a, &b), 0);

        TF_CHECK_INT(iot_datetime_add_seconds(&b, 2), IOT_OK);
        TF_CHECK_INT(b.year, 2024);
        TF_CHECK_INT(b.month, 1);
        TF_CHECK_INT(b.day, 1);
        TF_CHECK_INT(b.hour, 0);
        TF_CHECK_INT(b.minute, 0);
        TF_CHECK_INT(b.second, 1);
        TF_CHECK_INT(iot_datetime_compare(&a, &b), -1);
    }
    tf_end();

    tf_begin("datetime/软件时钟");
    {
        iot_datetime_t set_dt, get_dt;
        memset(&set_dt, 0, sizeof(set_dt));
        set_dt.year = 2025; set_dt.month = 6; set_dt.day = 15;
        set_dt.hour = 12; set_dt.minute = 30; set_dt.second = 45;
        iot_datetime_init();
        TF_CHECK_INT(iot_datetime_set(&set_dt), IOT_OK);
        TF_CHECK_INT(iot_datetime_get(&get_dt), IOT_OK);
        TF_CHECK_INT(get_dt.year, 2025);
        TF_CHECK_INT(get_dt.month, 6);
        TF_CHECK_INT(get_dt.day, 15);
        TF_CHECK_INT(get_dt.second, 45);
        TF_CHECK_INT(iot_datetime_set(NULL), -1);
    }
    tf_end();
}

static int s_cron_hits = 0;

static void cron_cb(void *user)
{
    (void)user;
    s_cron_hits++;
}

static void test_cron(void)
{
    iot_datetime_t now;
    iot_id_t id = 0;

    tf_begin("datetime/cron 精确触发");
    iot_cron_init();
    s_cron_hits = 0;
    TF_CHECK_INT(iot_cron_add("08:00:00", cron_cb, NULL, &id), IOT_OK);

    memset(&now, 0, sizeof(now));
    now.year = 2024; now.month = 1; now.day = 1;
    now.hour = 8; now.minute = 0; now.second = 0;
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(s_cron_hits, 1);

    /* 同一时间戳不重复触发 */
    iot_cron_process(&now);
    TF_CHECK_INT(s_cron_hits, 1);

    /* 秒数不匹配不触发 */
    now.second = 1;
    iot_cron_process(&now);
    TF_CHECK_INT(s_cron_hits, 1);
    tf_end();

    tf_begin("datetime/cron 通配与移除");
    s_cron_hits = 0;
    TF_CHECK_INT(iot_cron_add("*:*:05", cron_cb, NULL, NULL), IOT_OK);
    memset(&now, 0, sizeof(now));
    now.year = 2024; now.month = 5; now.day = 2;
    now.hour = 13; now.minute = 27; now.second = 5;
    iot_cron_process(&now);
    TF_CHECK_INT(s_cron_hits, 1);

    TF_CHECK_INT(iot_cron_remove(id), IOT_OK);
    s_cron_hits = 0;
    now.second = 0;
    iot_cron_process(&now);
    TF_CHECK_INT(s_cron_hits, 0);
    tf_end();

    tf_begin("datetime/cron 非法表达式");
    TF_CHECK_INT(iot_cron_add("bad", cron_cb, NULL, NULL), -1);
    TF_CHECK_INT(iot_cron_add(NULL, cron_cb, NULL, NULL), -1);
    TF_CHECK_INT(iot_cron_process(NULL), -1);
    tf_end();
}

void test_datetime(void)
{
    test_calendar();
    test_timestamp();
    test_cron();
}
