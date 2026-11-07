/* iot_datetime 单元测试：软件时钟、日历换算、时间戳互转与时间加减。 */
#include "test_framework.h"
#include "iot_datetime.h"

#include <stdint.h>

static iot_datetime_t make_dt(uint16_t y, uint8_t mo, uint8_t d,
                              uint8_t h, uint8_t mi, uint8_t s)
{
    iot_datetime_t dt;
    dt.year = y;
    dt.month = mo;
    dt.day = d;
    dt.hour = h;
    dt.minute = mi;
    dt.second = s;
    dt.millisecond = 0;
    dt.weekday = 0xFF;
    return dt;
}

static void test_clock(void)
{
    iot_datetime_t dt;
    iot_datetime_t bad;

    tf_begin("datetime: init/get 默认 1970-01-01");
    TF_CHECK_INT(iot_datetime_init(), IOT_OK);
    memset(&dt, 0, sizeof(dt));
    TF_CHECK_INT(iot_datetime_get(&dt), IOT_OK);
    TF_CHECK_INT(dt.year, 1970);
    TF_CHECK_INT(dt.month, 1);
    TF_CHECK_INT(dt.day, 1);
    TF_CHECK_INT(dt.hour, 0);
    TF_CHECK_INT(dt.minute, 0);
    TF_CHECK_INT(dt.second, 0);
    TF_CHECK_INT(dt.millisecond, 0);
    TF_CHECK_INT(dt.weekday, 4);
    tf_end();

    tf_begin("datetime: set 更新并重算星期");
    dt = make_dt(2024, 1, 1, 12, 30, 45);
    dt.millisecond = 500;
    TF_CHECK_INT(iot_datetime_set(&dt), IOT_OK);
    TF_CHECK_INT(iot_datetime_get(&dt), IOT_OK);
    TF_CHECK_INT(dt.year, 2024);
    TF_CHECK_INT(dt.month, 1);
    TF_CHECK_INT(dt.day, 1);
    TF_CHECK_INT(dt.hour, 12);
    TF_CHECK_INT(dt.minute, 30);
    TF_CHECK_INT(dt.second, 45);
    TF_CHECK_INT(dt.weekday, 1); /* 2024-01-01 星期一 */
    tf_end();

    tf_begin("datetime: get/set 非法参数");
    TF_CHECK_INT(iot_datetime_get(NULL), -1);
    TF_CHECK_INT(iot_datetime_set(NULL), -1);
    bad = make_dt(2024, 13, 1, 0, 0, 0);
    TF_CHECK_INT(iot_datetime_set(&bad), -1);
    tf_end();
}

static void test_calendar(void)
{
    tf_begin("datetime: 闰年判断");
    TF_CHECK(iot_datetime_is_leap_year(2000) == true);
    TF_CHECK(iot_datetime_is_leap_year(1900) == false);
    TF_CHECK(iot_datetime_is_leap_year(2024) == true);
    TF_CHECK(iot_datetime_is_leap_year(2023) == false);
    TF_CHECK(iot_datetime_is_leap_year(4) == true);
    TF_CHECK(iot_datetime_is_leap_year(100) == false);
    tf_end();

    tf_begin("datetime: 月份天数");
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 1), 31);
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 2), 29);
    TF_CHECK_INT(iot_datetime_days_in_month(2023, 2), 28);
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 4), 30);
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 12), 31);
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 0), 0);
    TF_CHECK_INT(iot_datetime_days_in_month(2024, 13), 0);
    tf_end();

    tf_begin("datetime: 参数校验");
    {
        iot_datetime_t dt = make_dt(2024, 2, 29, 23, 59, 59);
        TF_CHECK(iot_datetime_is_valid(&dt) == true);
        TF_CHECK(iot_datetime_is_valid(NULL) == false);

        dt = make_dt(2023, 2, 29, 0, 0, 0);
        TF_CHECK(iot_datetime_is_valid(&dt) == false);
        dt = make_dt(2024, 0, 1, 0, 0, 0);
        TF_CHECK(iot_datetime_is_valid(&dt) == false);
        dt = make_dt(2024, 1, 0, 0, 0, 0);
        TF_CHECK(iot_datetime_is_valid(&dt) == false);
        dt = make_dt(2024, 1, 1, 24, 0, 0);
        TF_CHECK(iot_datetime_is_valid(&dt) == false);
        dt = make_dt(2024, 1, 1, 0, 60, 0);
        TF_CHECK(iot_datetime_is_valid(&dt) == false);
        dt = make_dt(2024, 1, 1, 0, 0, 60);
        TF_CHECK(iot_datetime_is_valid(&dt) == false);
        dt = make_dt(2024, 1, 1, 0, 0, 0);
        dt.millisecond = 1000;
        TF_CHECK(iot_datetime_is_valid(&dt) == false);
        dt.millisecond = 999;
        TF_CHECK(iot_datetime_is_valid(&dt) == true);
    }
    tf_end();

    tf_begin("datetime: 星期计算");
    TF_CHECK_INT(iot_datetime_weekday(1970, 1, 1), 4);
    TF_CHECK_INT(iot_datetime_weekday(2000, 1, 1), 6);
    TF_CHECK_INT(iot_datetime_weekday(2024, 1, 1), 1);
    TF_CHECK_INT(iot_datetime_weekday(2023, 12, 31), 0);
    TF_CHECK_INT(iot_datetime_weekday(2024, 2, 29), 4);
    tf_end();
}

static void test_timestamp(void)
{
    iot_datetime_t dt;
    iot_datetime_t back;
    int64_t ts = 0;

    tf_begin("datetime: to_timestamp 已知值");
    dt = make_dt(1970, 1, 1, 0, 0, 0);
    TF_CHECK_INT(iot_datetime_to_timestamp(&dt, &ts), IOT_OK);
    TF_CHECK_INT(ts, 0);

    dt = make_dt(2000, 1, 1, 0, 0, 0);
    TF_CHECK_INT(iot_datetime_to_timestamp(&dt, &ts), IOT_OK);
    TF_CHECK_INT(ts, 946684800);

    dt = make_dt(1970, 1, 1, 0, 0, 1);
    TF_CHECK_INT(iot_datetime_to_timestamp(&dt, &ts), IOT_OK);
    TF_CHECK_INT(ts, 1);
    tf_end();

    tf_begin("datetime: to_timestamp 非法参数");
    dt = make_dt(2024, 13, 1, 0, 0, 0);
    TF_CHECK_INT(iot_datetime_to_timestamp(&dt, &ts), -1);
    dt = make_dt(2024, 1, 1, 0, 0, 0);
    TF_CHECK_INT(iot_datetime_to_timestamp(&dt, NULL), -1);
    TF_CHECK_INT(iot_datetime_to_timestamp(NULL, &ts), -1);
    tf_end();

    tf_begin("datetime: timestamp_to_datetime 正常值");
    TF_CHECK_INT(iot_timestamp_to_datetime(0, &back), IOT_OK);
    TF_CHECK_INT(back.year, 1970);
    TF_CHECK_INT(back.month, 1);
    TF_CHECK_INT(back.day, 1);
    TF_CHECK_INT(back.weekday, 4);

    TF_CHECK_INT(iot_timestamp_to_datetime(946684800, &back), IOT_OK);
    TF_CHECK_INT(back.year, 2000);
    TF_CHECK_INT(back.month, 1);
    TF_CHECK_INT(back.day, 1);
    TF_CHECK_INT(back.weekday, 6);

    TF_CHECK_INT(iot_timestamp_to_datetime(951782400, &back), IOT_OK); /* 2000-02-29 */
    TF_CHECK_INT(back.year, 2000);
    TF_CHECK_INT(back.month, 2);
    TF_CHECK_INT(back.day, 29);
    tf_end();

    tf_begin("datetime: timestamp_to_datetime 负时间戳");
    TF_CHECK_INT(iot_timestamp_to_datetime(-1, &back), IOT_OK);
    TF_CHECK_INT(back.year, 1969);
    TF_CHECK_INT(back.month, 12);
    TF_CHECK_INT(back.day, 31);
    TF_CHECK_INT(back.hour, 23);
    TF_CHECK_INT(back.minute, 59);
    TF_CHECK_INT(back.second, 59);
    TF_CHECK_INT(back.weekday, 3);
    tf_end();

    tf_begin("datetime: timestamp_to_datetime 非法参数");
    TF_CHECK_INT(iot_timestamp_to_datetime(0, NULL), -1);
    tf_end();

    tf_begin("datetime: 时间戳往返");
    dt = make_dt(2024, 2, 29, 12, 34, 56);
    TF_CHECK_INT(iot_datetime_to_timestamp(&dt, &ts), IOT_OK);
    TF_CHECK_INT(iot_timestamp_to_datetime(ts, &back), IOT_OK);
    TF_CHECK_INT(back.year, dt.year);
    TF_CHECK_INT(back.month, dt.month);
    TF_CHECK_INT(back.day, dt.day);
    TF_CHECK_INT(back.hour, dt.hour);
    TF_CHECK_INT(back.minute, dt.minute);
    TF_CHECK_INT(back.second, dt.second);
    tf_end();
}

static void test_compare_add(void)
{
    iot_datetime_t a;
    iot_datetime_t b;
    iot_datetime_t bad;

    tf_begin("datetime: compare");
    a = make_dt(2024, 1, 1, 0, 0, 0);
    b = make_dt(2024, 1, 1, 0, 0, 0);
    TF_CHECK_INT(iot_datetime_compare(&a, &b), 0);

    b = make_dt(2024, 1, 1, 0, 0, 1);
    TF_CHECK_INT(iot_datetime_compare(&a, &b), -1);
    TF_CHECK_INT(iot_datetime_compare(&b, &a), 1);

    TF_CHECK_INT(iot_datetime_compare(NULL, &b), -1);
    TF_CHECK_INT(iot_datetime_compare(&a, NULL), -1);

    bad = make_dt(2024, 13, 1, 0, 0, 0);
    TF_CHECK_INT(iot_datetime_compare(&bad, &b), -1);
    tf_end();

    tf_begin("datetime: add_seconds 跨月/跨年");
    a = make_dt(2024, 1, 31, 23, 59, 59);
    TF_CHECK_INT(iot_datetime_add_seconds(&a, 1), IOT_OK);
    TF_CHECK_INT(a.year, 2024);
    TF_CHECK_INT(a.month, 2);
    TF_CHECK_INT(a.day, 1);
    TF_CHECK_INT(a.hour, 0);
    TF_CHECK_INT(a.minute, 0);
    TF_CHECK_INT(a.second, 0);
    TF_CHECK_INT(a.weekday, 4); /* 2024-02-01 星期四 */

    a = make_dt(2024, 3, 1, 0, 0, 0);
    TF_CHECK_INT(iot_datetime_add_seconds(&a, -1), IOT_OK);
    TF_CHECK_INT(a.year, 2024);
    TF_CHECK_INT(a.month, 2);
    TF_CHECK_INT(a.day, 29);
    TF_CHECK_INT(a.hour, 23);
    TF_CHECK_INT(a.minute, 59);
    TF_CHECK_INT(a.second, 59);

    a = make_dt(2023, 12, 31, 23, 59, 59);
    TF_CHECK_INT(iot_datetime_add_seconds(&a, 1), IOT_OK);
    TF_CHECK_INT(a.year, 2024);
    TF_CHECK_INT(a.month, 1);
    TF_CHECK_INT(a.day, 1);
    TF_CHECK_INT(a.weekday, 1);
    tf_end();

    tf_begin("datetime: add_seconds 保留毫秒");
    a = make_dt(1970, 1, 1, 0, 0, 0);
    a.millisecond = 500;
    TF_CHECK_INT(iot_datetime_add_seconds(&a, 60), IOT_OK);
    TF_CHECK_INT(a.hour, 0);
    TF_CHECK_INT(a.minute, 1);
    TF_CHECK_INT(a.second, 0);
    TF_CHECK_INT(a.millisecond, 500);
    tf_end();

    tf_begin("datetime: add_seconds 非法参数");
    TF_CHECK_INT(iot_datetime_add_seconds(NULL, 1), -1);
    bad = make_dt(2024, 0, 1, 0, 0, 0);
    TF_CHECK_INT(iot_datetime_add_seconds(&bad, 1), -1);
    tf_end();
}

void ut_iot_datetime(void)
{
    test_clock();
    test_calendar();
    test_timestamp();
    test_compare_add();
}
