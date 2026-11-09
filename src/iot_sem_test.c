/*
 * iot_sem_test.c - 信号量测试（对应 iot_sem.c）
 *
 * 覆盖：计数语义、阻塞唤醒与直接交接、等待超时、计数器上限、FromISR。
 */

#include "test.h"
#include "iot.h"

static iot_sem_t s_sem;
static iot_task_t s_giver;
static uint8_t s_giver_stack[2048];
static volatile uint32_t s_give_count;

static void giver_entry(void *arg)
{
    (void)arg;
    iot_sleep_ms(2);
    s_give_count++;
    (void)iot_sem_give(&s_sem);
}

static void test_sem_basic(void)
{
    tf_begin("iot/信号量-计数语义");

    TF_CHECK_INT(iot_sem_init(NULL, 1U), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_sem_init(&s_sem, 1U), IOT_OK);

    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(s_sem.count, 0);
    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    TF_CHECK_INT(iot_sem_give(&s_sem), IOT_OK);
    TF_CHECK_INT(s_sem.count, 1);
    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    /* 未初始化对象应被拒绝 */
    {
        iot_sem_t bad;
        memset(&bad, 0, sizeof(bad));
        TF_CHECK_INT(iot_sem_take(&bad, IOT_TIMEOUT_NONE), IOT_INVALID_PARAM);
        TF_CHECK_INT(iot_sem_give(&bad), IOT_INVALID_PARAM);
    }

    tf_end();
}

static void test_sem_count_limit(void)
{
    uint32_t i;

    tf_begin("iot/信号量-计数上限");

    TF_CHECK_INT(iot_sem_init(&s_sem, 0U), IOT_OK);
    for (i = 0; i < 5U; i++) {
        TF_CHECK_INT(iot_sem_give(&s_sem), IOT_OK);
    }
    TF_CHECK_INT(s_sem.count, 5);
    for (i = 0; i < 5U; i++) {
        TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_OK);
    }
    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    tf_end();
}

static void test_sem_block(void)
{
    uint32_t t0;

    tf_begin("iot/信号量-阻塞唤醒");

    s_give_count = 0U;
    TF_CHECK_INT(iot_sem_init(&s_sem, 0U), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_giver, "giver", 2, giver_entry, NULL,
                                 s_giver_stack, sizeof(s_giver_stack)),
                 IOT_OK);

    t0 = iot_tick_get();
    TF_CHECK_INT(iot_sem_take(&s_sem, 100U), IOT_OK);
    TF_CHECK((iot_tick_get() - t0) >= 2U);
    TF_CHECK_INT(s_give_count, 1);
    /* 直接交接：计数保持 0 */
    TF_CHECK_INT(s_sem.count, 0);

    tf_end();
}

static void test_sem_timeout(void)
{
    uint32_t t0;

    tf_begin("iot/信号量-等待超时");

    TF_CHECK_INT(iot_sem_init(&s_sem, 0U), IOT_OK);
    t0 = iot_tick_get();
    TF_CHECK_INT(iot_sem_take(&s_sem, 5U), IOT_TIMEOUT);
    TF_CHECK((iot_tick_get() - t0) >= 5U);

    tf_end();
}

static void test_sem_from_isr(void)
{
    bool need_yield = false;

    tf_begin("iot/信号量-FromISR");

    TF_CHECK_INT(iot_sem_init(&s_sem, 0U), IOT_OK);
    TF_CHECK_INT(iot_sem_give_from_isr(&s_sem, &need_yield), IOT_OK);
    TF_CHECK(!need_yield);
    TF_CHECK_INT(s_sem.count, 1);
    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(iot_sem_give_from_isr(NULL, &need_yield), IOT_INVALID_PARAM);

    tf_end();
}

void iot_test_sem(void)
{
    test_sem_basic();
    test_sem_count_limit();
    test_sem_block();
    test_sem_timeout();
    test_sem_from_isr();
}
