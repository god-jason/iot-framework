/* iot_cron 单元测试：表达式解析、定时触发、去重、移除与容量。 */
#include "test_framework.h"
#include "iot_cron.h"

#include <stdint.h>

static int g_fires;
static int g_user_hits;

static void cron_cb(void *user)
{
    g_fires++;
    if (user != NULL) {
        (*(int *)user)++;
    }
}

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
    dt.weekday = 0;
    return dt;
}

static void test_add_errors(void)
{
    iot_id_t id = 123;

    tf_begin("cron: add 非法参数");
    TF_CHECK_INT(iot_cron_init(), IOT_OK);
    TF_CHECK_INT(iot_cron_add(NULL, cron_cb, NULL, &id), -1);
    TF_CHECK_INT(iot_cron_add("08:00:00", NULL, NULL, &id), -1);
    TF_CHECK_INT(id, 123);
    tf_end();

    tf_begin("cron: add 非法表达式");
    TF_CHECK_INT(iot_cron_add("08:00", cron_cb, NULL, &id), -1);        /* 字段不足 */
    TF_CHECK_INT(iot_cron_add("", cron_cb, NULL, &id), -1);
    TF_CHECK_INT(iot_cron_add("::", cron_cb, NULL, &id), -1);           /* 空字段 */
    TF_CHECK_INT(iot_cron_add("24:00:00", cron_cb, NULL, &id), -1);     /* 小时越界 */
    TF_CHECK_INT(iot_cron_add("08:60:00", cron_cb, NULL, &id), -1);     /* 分钟越界 */
    TF_CHECK_INT(iot_cron_add("08:00:60", cron_cb, NULL, &id), -1);     /* 秒越界 */
    TF_CHECK_INT(iot_cron_add("xx:00:00", cron_cb, NULL, &id), -1);     /* 非数字 */
    tf_end();

    tf_begin("cron: add 成功返回递增 ID");
    TF_CHECK_INT(iot_cron_init(), IOT_OK);
    id = 0xFFFFFFFFu;
    TF_CHECK_INT(iot_cron_add("08:30:15", cron_cb, NULL, &id), IOT_OK);
    TF_CHECK_INT(id, 0);
    TF_CHECK_INT(iot_cron_add("*:00:00", cron_cb, NULL, &id), IOT_OK);
    TF_CHECK_INT(id, 1);
    TF_CHECK_INT(iot_cron_add("23:59:59", cron_cb, NULL, NULL), IOT_OK);
    tf_end();
}

static void test_process(void)
{
    iot_datetime_t now;

    g_fires = 0;
    g_user_hits = 0;

    tf_begin("cron: 到点触发且同一秒只触发一次");
    TF_CHECK_INT(iot_cron_init(), IOT_OK);
    TF_CHECK_INT(iot_cron_add("08:30:15", cron_cb, NULL, NULL), IOT_OK);

    now = make_dt(2024, 1, 1, 8, 30, 14);
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(g_fires, 0);

    now = make_dt(2024, 1, 1, 8, 30, 15);
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(g_fires, 1);

    TF_CHECK_INT(iot_cron_process(&now), IOT_OK); /* 同一时间戳不重复触发 */
    TF_CHECK_INT(g_fires, 1);

    now = make_dt(2024, 1, 2, 8, 30, 15); /* 次日同一时刻 */
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(g_fires, 2);
    tf_end();

    tf_begin("cron: 通配符每个不同时刻触发");
    g_fires = 0;
    TF_CHECK_INT(iot_cron_init(), IOT_OK);
    TF_CHECK_INT(iot_cron_add("*:*:*", cron_cb, NULL, NULL), IOT_OK);

    now = make_dt(2024, 1, 1, 0, 0, 0);
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(g_fires, 1);
    now = make_dt(2024, 1, 1, 0, 0, 1);
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(g_fires, 2);
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(g_fires, 2);
    tf_end();

    tf_begin("cron: user 指针透传");
    g_fires = 0;
    g_user_hits = 0;
    TF_CHECK_INT(iot_cron_init(), IOT_OK);
    TF_CHECK_INT(iot_cron_add("12:00:00", cron_cb, &g_user_hits, NULL), IOT_OK);
    now = make_dt(2024, 6, 1, 12, 0, 0);
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(g_fires, 1);
    TF_CHECK_INT(g_user_hits, 1);
    tf_end();

    tf_begin("cron: process 非法参数");
    TF_CHECK_INT(iot_cron_process(NULL), -1);
    now = make_dt(2024, 13, 1, 0, 0, 0);
    TF_CHECK_INT(iot_cron_process(&now), -1);
    tf_end();
}

static void test_remove(void)
{
    iot_datetime_t now;
    iot_id_t id;

    g_fires = 0;

    tf_begin("cron: remove 合法与非法 ID");
    TF_CHECK_INT(iot_cron_init(), IOT_OK);
    TF_CHECK_INT(iot_cron_remove(0), -1);
    TF_CHECK_INT(iot_cron_remove(8), -1);
    TF_CHECK_INT(iot_cron_remove(99), -1);

    TF_CHECK_INT(iot_cron_add("10:00:00", cron_cb, NULL, &id), IOT_OK);
    TF_CHECK_INT(iot_cron_remove(id), IOT_OK);
    TF_CHECK_INT(iot_cron_remove(id), -1);

    now = make_dt(2024, 1, 1, 10, 0, 0);
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(g_fires, 0);
    tf_end();
}

static void test_capacity(void)
{
    int i;
    iot_id_t id;
    iot_datetime_t now;

    g_fires = 0;

    tf_begin("cron: 任务容量上限 8");
    TF_CHECK_INT(iot_cron_init(), IOT_OK);
    for (i = 0; i < 8; i++) {
        char expr[9];
        expr[0] = (char)('0' + i % 10);
        expr[1] = ':';
        expr[2] = '0';
        expr[3] = '0';
        expr[4] = ':';
        expr[5] = '0';
        expr[6] = '0';
        expr[7] = '\0';
        TF_CHECK_INT(iot_cron_add(expr, cron_cb, NULL, &id), IOT_OK);
        TF_CHECK_INT(id, i);
    }
    TF_CHECK_INT(iot_cron_add("09:00:00", cron_cb, NULL, &id), -1);

    TF_CHECK_INT(iot_cron_init(), IOT_OK); /* init 释放全部槽位 */
    TF_CHECK_INT(iot_cron_add("09:00:00", cron_cb, NULL, &id), IOT_OK);
    TF_CHECK_INT(id, 0);

    now = make_dt(2024, 1, 1, 9, 0, 0);
    TF_CHECK_INT(iot_cron_process(&now), IOT_OK);
    TF_CHECK_INT(g_fires, 1);
    tf_end();
}

void ut_iot_cron(void)
{
    test_add_errors();
    test_process();
    test_remove();
    test_capacity();
}
