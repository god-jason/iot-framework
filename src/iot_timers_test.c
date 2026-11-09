/*
 * iot_timers_test.c - 软件定时器测试（对应 iot_timers.c）
 *
 * 覆盖：单次/周期/停止、多周期并行、运行中重启、
 *       同一时刻按启动顺序回调。
 */

#include "test.h"
#include "iot.h"

#include <string.h>

/* ============================== 静态对象 ============================== */

static iot_timer_t s_timer_fast;
static iot_timer_t s_timer_slow;
static iot_timer_t s_timer_ord1;
static iot_timer_t s_timer_ord2;

static volatile uint32_t s_timer_one;
static volatile uint32_t s_timer_periodic;
static volatile uint32_t s_timer_stopped;
static volatile uint32_t s_fast_count;
static volatile uint32_t s_slow_count;

static char s_timer_order[8];
static int s_timer_order_len;

/* ============================== 回调 ============================== */

static void timer_cb(void *arg)
{
    (*(volatile uint32_t *)arg)++;
}

static void fast_timer_cb(void *arg)
{
    (void)arg;
    s_fast_count++;
}

static void slow_timer_cb(void *arg)
{
    (void)arg;
    s_slow_count++;
}

/* 同刻定时器顺序回调：把参数字符追加到记录缓冲 */
static void order_timer_cb(void *arg)
{
    char c = (char)(intptr_t)arg;

    if (s_timer_order_len < (int)sizeof(s_timer_order) - 1) {
        s_timer_order[s_timer_order_len++] = c;
    }
}

/* ============================== 单项测试 ============================== */

/* 1. 单次 / 周期 / 停止 */
static void test_timers(void)
{
    iot_timer_t one;
    iot_timer_t period;
    iot_timer_t stopped;

    tf_begin("iot/软件定时器");

    s_timer_one = 0U;
    s_timer_periodic = 0U;
    s_timer_stopped = 0U;

    /* 单次：10 tick 后触发一次 */
    TF_CHECK_INT(iot_timer_init(&one, "one", timer_cb, (void *)&s_timer_one,
                                 10U, false), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&one, 10U), IOT_OK);
    iot_sleep_ms(15U);
    TF_CHECK_INT((int)s_timer_one, 1);
    iot_sleep_ms(20U);
    TF_CHECK_INT((int)s_timer_one, 1);

    /* 周期：35 tick 内至少触发 3 次 */
    TF_CHECK_INT(iot_timer_init(&period, "period", timer_cb,
                                 (void *)&s_timer_periodic, 10U, true), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&period, 10U), IOT_OK);
    iot_sleep_ms(35U);
    TF_CHECK_INT(iot_timer_stop(&period), IOT_OK);
    TF_CHECK(s_timer_periodic >= 3U);

    /* 启动后立即停止：不应触发 */
    TF_CHECK_INT(iot_timer_init(&stopped, "stopped", timer_cb,
                                 (void *)&s_timer_stopped, 100U, true), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&stopped, 100U), IOT_OK);
    TF_CHECK_INT(iot_timer_stop(&stopped), IOT_OK);
    iot_sleep_ms(20U);
    TF_CHECK_INT((int)s_timer_stopped, 0);

    tf_end();
}

/* 2. 多周期并行 */
static void test_timers_multi(void)
{
    tf_begin("iot/定时器-多周期");

    s_fast_count = 0;
    s_slow_count = 0;

    TF_CHECK_INT(iot_timer_init(&s_timer_fast, "fast", fast_timer_cb, NULL,
                                 5U, true), IOT_OK);
    TF_CHECK_INT(iot_timer_init(&s_timer_slow, "slow", slow_timer_cb, NULL,
                                 7U, true), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_timer_fast, 5U), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_timer_slow, 7U), IOT_OK);

    iot_sleep_ms(35U);

    TF_CHECK_INT(iot_timer_stop(&s_timer_fast), IOT_OK);
    TF_CHECK_INT(iot_timer_stop(&s_timer_slow), IOT_OK);

    TF_CHECK_INT((int)s_fast_count, 7);   /* 35 / 5 */
    TF_CHECK_INT((int)s_slow_count, 5);   /* 35 / 7 */

    tf_end();
}

/* 3. 重启与停止 */
static void test_timers_restart(void)
{
    tf_begin("iot/定时器-重启与停止");

    s_fast_count = 0;
    TF_CHECK_INT(iot_timer_init(&s_timer_fast, "restart", fast_timer_cb, NULL,
                                 10U, true), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_timer_fast, 10U), IOT_OK);
    iot_sleep_ms(15U);
    TF_CHECK_INT((int)s_fast_count, 1);

    /* 运行中重启：周期改为 20，从当前时刻重新计时 */
    TF_CHECK_INT(iot_timer_start(&s_timer_fast, 20U), IOT_OK);
    iot_sleep_ms(25U);
    TF_CHECK_INT((int)s_fast_count, 2);

    /* 停止后不再触发 */
    TF_CHECK_INT(iot_timer_stop(&s_timer_fast), IOT_OK);
    iot_sleep_ms(30U);
    TF_CHECK_INT((int)s_fast_count, 2);

    tf_end();
}

/* 4. 同一时刻按启动顺序回调 */
static void test_timers_order(void)
{
    tf_begin("iot/定时器-同刻顺序");

    s_timer_order_len = 0;
    memset(s_timer_order, 0, sizeof(s_timer_order));

    TF_CHECK_INT(iot_timer_init(&s_timer_ord1, "ord1", order_timer_cb,
                                 (void *)(intptr_t)'A', 10U, false), IOT_OK);
    TF_CHECK_INT(iot_timer_init(&s_timer_ord2, "ord2", order_timer_cb,
                                 (void *)(intptr_t)'B', 10U, false), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_timer_ord1, 10U), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_timer_ord2, 10U), IOT_OK);

    iot_sleep_ms(15U);

    s_timer_order[s_timer_order_len] = '\0';
    TF_CHECK_STR(s_timer_order, "AB");

    tf_end();
}

/* ============================== 对外入口 ============================== */

void iot_test_timer(void)
{
    test_timers();
    test_timers_multi();
    test_timers_restart();
    test_timers_order();
}
