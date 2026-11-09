/*
 * iot_timers_test.c - 软件定时器测试（对应 iot_timers.c）
 *
 * 覆盖：单次/周期/停止、参数校验、多周期并行、运行中重启、
 *       同一时刻按启动顺序回调。
 */

#include "test.h"
#include "iot.h"

static iot_timer_t s_fast;
static iot_timer_t s_slow;
static iot_timer_t s_ord1;
static iot_timer_t s_ord2;

static volatile uint32_t s_one;
static volatile uint32_t s_periodic;
static volatile uint32_t s_fast_count;
static volatile uint32_t s_slow_count;

static char s_order[8];
static int s_order_len;

static void count_cb(void *arg)
{
    (*(volatile uint32_t *)arg)++;
}

static void fast_cb(void *arg)
{
    (void)arg;
    s_fast_count++;
}

static void slow_cb(void *arg)
{
    (void)arg;
    s_slow_count++;
}

static void order_cb(void *arg)
{
    char c = (char)(intptr_t)arg;

    if (s_order_len < (int)sizeof(s_order) - 1) {
        s_order[s_order_len++] = c;
    }
}

#if IOT_USE_TIMER_TASK
static const char *s_cb_task_name;

static void context_cb(void *arg)
{
    iot_task_t *cur;

    (void)arg;
    cur = iot_current();
    s_cb_task_name = (cur != NULL) ? cur->name : NULL;
}
#endif

static void test_timer_init(void)
{
    iot_timer_t t;

    tf_begin("iot/定时器-初始化校验");

    TF_CHECK_INT(iot_timer_init(NULL, "t", count_cb, NULL, 10U, false),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_timer_init(&t, "t", NULL, NULL, 10U, false),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_timer_init(&t, "t", count_cb, NULL, 0U, false),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_timer_init(&t, "t", count_cb, NULL, 10U, false), IOT_OK);
    TF_CHECK(!t.running);
    TF_CHECK_INT(t.period, 10);
    TF_CHECK_STR(t.name, "t");

    TF_CHECK_INT(iot_timer_start(NULL, 10U), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_timer_start(&t, 0U), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_timer_stop(NULL), IOT_INVALID_PARAM);

    tf_end();
}

static void test_timer_once(void)
{
    iot_timer_t one;

    tf_begin("iot/定时器-单次触发");

    s_one = 0U;
    TF_CHECK_INT(iot_timer_init(&one, "one", count_cb, (void *)&s_one,
                                10U, false), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&one, 10U), IOT_OK);
    TF_CHECK(one.running);

    iot_sleep_ms(15U);
    TF_CHECK_INT((int)s_one, 1);
    TF_CHECK(!one.running);          /* 单次触发后自动停止 */

    iot_sleep_ms(20U);
    TF_CHECK_INT((int)s_one, 1);

    tf_end();
}

static void test_timer_periodic(void)
{
    iot_timer_t period;

    tf_begin("iot/定时器-周期与停止");

    s_periodic = 0U;
    TF_CHECK_INT(iot_timer_init(&period, "period", count_cb,
                                (void *)&s_periodic, 10U, true), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&period, 10U), IOT_OK);
    iot_sleep_ms(35U);
    TF_CHECK_INT(iot_timer_stop(&period), IOT_OK);
    TF_CHECK(!period.running);
    TF_CHECK(s_periodic >= 3U);

    {
        uint32_t snapshot = s_periodic;
        iot_sleep_ms(30U);
        TF_CHECK_INT((int)s_periodic, (int)snapshot);   /* 停止后不再触发 */
    }

    tf_end();
}

static void test_timer_multi(void)
{
    tf_begin("iot/定时器-多周期并行");

    s_fast_count = 0U;
    s_slow_count = 0U;

    TF_CHECK_INT(iot_timer_init(&s_fast, "fast", fast_cb, NULL, 5U, true),
                 IOT_OK);
    TF_CHECK_INT(iot_timer_init(&s_slow, "slow", slow_cb, NULL, 7U, true),
                 IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_fast, 5U), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_slow, 7U), IOT_OK);

    iot_sleep_ms(35U);
    /*
     * 任务模式下，35ms 到期的回调可能刚入待执行队列、尚未被定时器任务处理；
     * 再睡 1ms 让高优先级定时器任务先清空队列，再停止定时器。
     */
    iot_sleep_ms(1U);

    TF_CHECK_INT(iot_timer_stop(&s_fast), IOT_OK);
    TF_CHECK_INT(iot_timer_stop(&s_slow), IOT_OK);

    TF_CHECK_INT((int)s_fast_count, 7);   /* 35 / 5 */
    TF_CHECK_INT((int)s_slow_count, 5);   /* 35 / 7 */

    tf_end();
}

static void test_timer_restart(void)
{
    tf_begin("iot/定时器-运行中重启");

    s_fast_count = 0U;
    TF_CHECK_INT(iot_timer_init(&s_fast, "restart", fast_cb, NULL, 10U, true),
                 IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_fast, 10U), IOT_OK);
    iot_sleep_ms(15U);
    TF_CHECK_INT((int)s_fast_count, 1);

    /* 重启：周期改为 20，从当前时刻重新计时 */
    TF_CHECK_INT(iot_timer_start(&s_fast, 20U), IOT_OK);
    iot_sleep_ms(25U);
    TF_CHECK_INT((int)s_fast_count, 2);
    TF_CHECK_INT(s_fast.period, 20);

    TF_CHECK_INT(iot_timer_stop(&s_fast), IOT_OK);
    iot_sleep_ms(30U);
    TF_CHECK_INT((int)s_fast_count, 2);

    tf_end();
}

static void test_timer_order(void)
{
    tf_begin("iot/定时器-同刻顺序");

    s_order_len = 0;
    memset(s_order, 0, sizeof(s_order));

    TF_CHECK_INT(iot_timer_init(&s_ord1, "ord1", order_cb,
                                (void *)(intptr_t)'A', 10U, false), IOT_OK);
    TF_CHECK_INT(iot_timer_init(&s_ord2, "ord2", order_cb,
                                (void *)(intptr_t)'B', 10U, false), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_ord1, 10U), IOT_OK);
    TF_CHECK_INT(iot_timer_start(&s_ord2, 10U), IOT_OK);

    iot_sleep_ms(15U);

    s_order[s_order_len] = '\0';
    TF_CHECK_STR(s_order, "AB");

    tf_end();
}

static void test_timer_context(void)
{
#if IOT_USE_TIMER_TASK
    iot_timer_t timer;

    tf_begin("iot/定时器-回调在定时器任务中执行");

    s_cb_task_name = NULL;
    TF_CHECK_INT(iot_timer_init(&timer, "ctx", context_cb, NULL, 5U, false),
                 IOT_OK);
    TF_CHECK_INT(iot_timer_start(&timer, 5U), IOT_OK);
    iot_sleep_ms(15U);

    TF_CHECK(s_cb_task_name != NULL);
    if (s_cb_task_name != NULL) {
        TF_CHECK_STR(s_cb_task_name, "timer");
    }

    tf_end();
#endif
}

void iot_test_timer(void)
{
    test_timer_init();
    test_timer_once();
    test_timer_periodic();
    test_timer_multi();
    test_timer_restart();
    test_timer_order();
    test_timer_context();
}
