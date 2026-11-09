/*
 * iot_test_sem.c - 计数信号量测试
 *
 * 覆盖：计数语义、阻塞等待后由其它任务直接交接、等待超时。
 */

#include "test.h"
#include "iot.h"

/* ============================== 静态对象 ============================== */

static iot_sem_t s_sem;
static iot_task_t s_giver;
static uint8_t s_giver_stack[2048];

/* ============================== 辅助任务 ============================== */

/* 延时 2 tick 后释放信号量，用于验证阻塞唤醒 */
static void giver_entry(void *arg)
{
    (void)arg;
    iot_sleep_ms(2);
    (void)iot_sem_give(&s_sem);
}

/* ============================== 单项测试 ============================== */

/* 1. 计数与快速路径 */
static void test_semaphore_basic(void)
{
    tf_begin("iot/信号量-计数");

    TF_CHECK_INT(iot_sem_init(&s_sem, 1U), IOT_OK);
    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_TIMEOUT);
    TF_CHECK_INT(iot_sem_give(&s_sem), IOT_OK);
    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(iot_sem_take(&s_sem, IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    tf_end();
}

/* 2. 阻塞等待 + 直接交接 */
static void test_semaphore_block(void)
{
    uint32_t t0;

    tf_begin("iot/信号量-阻塞唤醒");

    TF_CHECK_INT(iot_sem_init(&s_sem, 0U), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_giver, "giver", 2, giver_entry, NULL,
                                  s_giver_stack, sizeof(s_giver_stack)),
                 IOT_OK);

    t0 = iot_tick_get();
    TF_CHECK_INT(iot_sem_take(&s_sem, 100U), IOT_OK);
    TF_CHECK((iot_tick_get() - t0) >= 2U);

    tf_end();
}

/* 3. 等待超时 */
static void test_semaphore_timeout(void)
{
    uint32_t t0;

    tf_begin("iot/信号量-超时");

    TF_CHECK_INT(iot_sem_init(&s_sem, 0U), IOT_OK);
    t0 = iot_tick_get();
    TF_CHECK_INT(iot_sem_take(&s_sem, 5U), IOT_TIMEOUT);
    TF_CHECK((iot_tick_get() - t0) >= 5U);

    tf_end();
}

/* ============================== 对外入口 ============================== */

void iot_test_sem(void)
{
    test_semaphore_basic();
    test_semaphore_block();
    test_semaphore_timeout();
}
