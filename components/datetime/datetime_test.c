/*
 * datetime_test.c - 日期时间测试（对应 datetime.c）
 *
 * 覆盖：闰年与天数、字段校验、Unix 时间戳往返、星期推导、
 *       比较、秒级偏移、软件时钟读写。
 */

#include "test.h"
#include "datetime.h"

void ut_datetime(void)
{
    datetime_t dt;
    datetime_t dt2;
    int64_t ts = 0;

    tf_begin("datetime/闰年与月份天数");

    TF_CHECK(datetime_is_leap_year(2000U));
    TF_CHECK(!datetime_is_leap_year(1900U));
    TF_CHECK(datetime_is_leap_year(2024U));
    TF_CHECK(!datetime_is_leap_year(2023U));

    TF_CHECK_INT(datetime_days_in_month(2024U, 2U), 29);
    TF_CHECK_INT(datetime_days_in_month(2023U, 2U), 28);
    TF_CHECK_INT(datetime_days_in_month(2024U, 1U), 31);
    TF_CHECK_INT(datetime_days_in_month(2024U, 4U), 30);
    TF_CHECK_INT(datetime_days_in_month(2024U, 13U), 0);

    tf_end();

    tf_begin("datetime/字段校验与星期");

    memset(&dt, 0, sizeof(dt));
    dt.year = 2024U;
    dt.month = 2U;
    dt.day = 29U;
    TF_CHECK(datetime_is_valid(&dt));

    dt.day = 30U;
    TF_CHECK(!datetime_is_valid(&dt));

    dt.year = 2023U;
    dt.day = 29U;
    TF_CHECK(!datetime_is_valid(&dt));

    dt.year = 2024U; dt.month = 1U; dt.day = 1U;
    TF_CHECK_INT(datetime_weekday(2024U, 1U, 1U), 1);   /* 周一 */
    TF_CHECK_INT(datetime_weekday(2023U, 12U, 31U), 0); /* 周日 */
    TF_CHECK(!datetime_is_valid(NULL));

    tf_end();

    tf_begin("datetime/时间戳往返");

    memset(&dt, 0, sizeof(dt));
    dt.year = 1970U; dt.month = 1U; dt.day = 1U;
    TF_CHECK_INT(datetime_to_timestamp(&dt, &ts), 0);
    TF_CHECK(ts == 0);

    dt.year = 2024U; dt.month = 1U; dt.day = 1U;
    TF_CHECK_INT(datetime_to_timestamp(&dt, &ts), 0);
    TF_CHECK(ts == 1704067200LL);

    memset(&dt2, 0, sizeof(dt2));
    TF_CHECK_INT(timestamp_to_datetime(ts, &dt2), 0);
    TF_CHECK_INT(dt2.year, 2024);
    TF_CHECK_INT(dt2.month, 1);
    TF_CHECK_INT(dt2.day, 1);
    TF_CHECK_INT(dt2.weekday, 1);
    TF_CHECK_INT(dt2.millisecond, 0);

    TF_CHECK_INT(datetime_to_timestamp(NULL, &ts), -1);
    TF_CHECK_INT(datetime_to_timestamp(&dt, NULL), -1);
    TF_CHECK_INT(timestamp_to_datetime(0, NULL), -1);

    tf_end();

    tf_begin("datetime/比较与偏移");

    memset(&dt, 0, sizeof(dt));
    dt.year = 2024U; dt.month = 1U; dt.day = 1U;
    dt2 = dt;
    TF_CHECK_INT(datetime_compare(&dt, &dt2), 0);

    dt2.hour = 1U;
    TF_CHECK_INT(datetime_compare(&dt, &dt2), -1);
    TF_CHECK_INT(datetime_compare(&dt2, &dt), 1);

    /* 跨天与跨月 */
    dt2 = dt;
    dt2.day = 31U;
    TF_CHECK_INT(datetime_add_seconds(&dt2, 86400), 0);
    TF_CHECK_INT(dt2.month, 2);
    TF_CHECK_INT(dt2.day, 1);

    dt2 = dt;
    TF_CHECK_INT(datetime_add_seconds(&dt2, -1), 0);
    TF_CHECK_INT(dt2.year, 2023);
    TF_CHECK_INT(dt2.month, 12);
    TF_CHECK_INT(dt2.day, 31);
    TF_CHECK_INT(dt2.hour, 23);

    TF_CHECK_INT(datetime_add_seconds(NULL, 1), -1);

    tf_end();

    tf_begin("datetime/软件时钟读写");

    TF_CHECK_INT(datetime_init(), 0);
    TF_CHECK_INT(datetime_get(&dt), 0);
    TF_CHECK_INT(dt.year, 1970);

    dt.year = 2024U; dt.month = 6U; dt.day = 1U; dt.hour = 12U;
    TF_CHECK_INT(datetime_set(&dt), 0);
    TF_CHECK_INT(datetime_get(&dt2), 0);
    TF_CHECK_INT(dt2.year, 2024);
    TF_CHECK_INT(dt2.hour, 12);
    TF_CHECK_INT(dt2.weekday, 6);          /* 2024-06-01 为周六 */

    TF_CHECK_INT(datetime_get(NULL), -1);
    TF_CHECK_INT(datetime_set(NULL), -1);

    tf_end();
}
