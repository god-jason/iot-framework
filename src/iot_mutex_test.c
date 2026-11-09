/*
 * iot_mutex_test.c - 互斥量测试（对应 iot_mutex.c）
 *
 * 覆盖：基本加锁/解锁、非递归语义、所有权校验（非持有者解锁）、
 *       多任务并发保护（临界区内让出仍不丢更新）。
 */

#include "test.h"
#include "iot.h"

#define MUTEX_ITERS 50

/* ============================== 静态对象 ============================== */

static iot_mutex_t s_mutex;
static iot_sem_t s_done;
static iot_task_t s_mutex_helper;
static iot_task_t s_counter_a;
static iot_task_t s_counter_b;
static uint8_t s_mutex_helper_stack[2048];
static uint8_t s_counter_stack_a[2048];
static uint8_t s_counter_stack_b[2048];

static volatile uint32_t s_counter;
static volatile int s_helper_result;

/* ============================== 辅助任务 ============================== */

/* 非持有者尝试解锁，预期失败 */
static void mutex_helper_entry(void *arg)
{
    (void)arg;
    s_helper_result = (int)iot_mutex_unlock(&s_mutex);
}

/* 互斥量保护的自增任务：读-让出-写，验证临界区互斥 */
static void counter_entry(void *arg)
{
    uint32_t i;
    uint32_t tmp;

    (void)arg;
    for (i = 0U; i < MUTEX_ITERS; i++) {
        (void)iot_mutex_lock(&s_mutex, 1000U);
        tmp = s_counter;
        iot_yield();               /* 在临界区内让出，制造竞态窗口 */
        s_counter = tmp + 1U;
        (void)iot_mutex_unlock(&s_mutex);
    }
    (void)iot_sem_give(&s_done);
}

/* ============================== 单项测试 ============================== */

/* 1. 基本加锁/解锁 + 非递归 */
static void test_mutex_basic(void)
{
    tf_begin("iot/互斥量-基本");

    TF_CHECK_INT(iot_mutex_init(&s_mutex), IOT_OK);
    TF_CHECK_INT(iot_mutex_lock(&s_mutex, IOT_TIMEOUT_NONE), IOT_OK);
    /* 非递归：已持有者再次加锁应立即超时 */
    TF_CHECK_INT(iot_mutex_lock(&s_mutex, IOT_TIMEOUT_NONE), IOT_TIMEOUT);
    TF_CHECK_INT(iot_mutex_unlock(&s_mutex), IOT_OK);
    /* 已释放，非持有者解锁应报 IOT_NOT_OWNER */
    TF_CHECK_INT(iot_mutex_unlock(&s_mutex), IOT_NOT_OWNER);

    tf_end();
}

/* 2. 所有权校验（其它任务不能解锁） */
static void test_mutex_owner(void)
{
    tf_begin("iot/互斥量-所有权");

    TF_CHECK_INT(iot_mutex_init(&s_mutex), IOT_OK);
    TF_CHECK_INT(iot_mutex_lock(&s_mutex, IOT_TIMEOUT_NONE), IOT_OK);

    s_helper_result = 12345;
    TF_CHECK_INT(iot_task_create(&s_mutex_helper, "mhelper", 3,
                                  mutex_helper_entry, NULL,
                                  s_mutex_helper_stack,
                                  sizeof(s_mutex_helper_stack)),
                 IOT_OK);

    iot_sleep_ms(1U);              /* 让 helper 运行并尝试解锁 */
    TF_CHECK_INT(s_helper_result, IOT_NOT_OWNER);
    TF_CHECK_INT(iot_mutex_unlock(&s_mutex), IOT_OK);

    tf_end();
}

/* 3. 多任务并发保护 */
static void test_mutex_mutual_exclusion(void)
{
    tf_begin("iot/互斥量-并发互斥");

    s_counter = 0U;
    TF_CHECK_INT(iot_mutex_init(&s_mutex), IOT_OK);
    TF_CHECK_INT(iot_sem_init(&s_done, 0U), IOT_OK);

    TF_CHECK_INT(iot_task_create(&s_counter_a, "cnt-a", 3, counter_entry, NULL,
                                  s_counter_stack_a, sizeof(s_counter_stack_a)),
                 IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_counter_b, "cnt-b", 3, counter_entry, NULL,
                                  s_counter_stack_b, sizeof(s_counter_stack_b)),
                 IOT_OK);

    /* 等待两个任务都完成 */
    TF_CHECK_INT(iot_sem_take(&s_done, 2000U), IOT_OK);
    TF_CHECK_INT(iot_sem_take(&s_done, 2000U), IOT_OK);
    TF_CHECK_INT((int)s_counter, 2 * MUTEX_ITERS);

    tf_end();
}

/* ============================== 优先级继承 ============================== */

static iot_mutex_t s_inherit_mutex;
static iot_sem_t s_inherit_locked;
static iot_task_t s_inherit_low;
static iot_task_t s_inherit_high;
static uint8_t s_inherit_low_stack[4096];
static uint8_t s_inherit_high_stack[4096];

/* 低优先级任务：持锁后睡眠，制造优先级反转场景 */
static void inherit_low_entry(void *arg)
{
    (void)arg;

    (void)iot_mutex_lock(&s_inherit_mutex, IOT_TIMEOUT_NONE);
    (void)iot_sem_give(&s_inherit_locked);

    iot_sleep_ms(50);
    (void)iot_mutex_unlock(&s_inherit_mutex);

    for (;;) {
        iot_sleep_ms(1000);
    }
}

/* 高优先级任务：等低优先级持锁后尝试加锁，触发优先级继承 */
static void inherit_high_entry(void *arg)
{
    (void)arg;

    iot_sleep_ms(10);
    (void)iot_mutex_lock(&s_inherit_mutex, IOT_TIMEOUT_NONE);

    for (;;) {
        iot_sleep_ms(1000);
    }
}

/* 低优先级持有者应被临时提升到等待者优先级，解锁后回落 */
static void test_priority_inherit(void)
{
    tf_begin("iot/优先级继承");

    TF_CHECK_INT(iot_mutex_init(&s_inherit_mutex), IOT_OK);
    TF_CHECK_INT(iot_sem_init(&s_inherit_locked, 0U), IOT_OK);

    TF_CHECK_INT(iot_task_create(&s_inherit_low, "low", 5U, inherit_low_entry,
                                 NULL, s_inherit_low_stack,
                                 sizeof(s_inherit_low_stack)), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_inherit_high, "high", 2U, inherit_high_entry,
                                 NULL, s_inherit_high_stack,
                                 sizeof(s_inherit_high_stack)), IOT_OK);

    /* 等低优先级任务拿到锁 */
    TF_CHECK_INT(iot_sem_take(&s_inherit_locked, IOT_TIMEOUT_FOREVER), IOT_OK);

    /* 给高优先级任务一次加锁机会 */
    iot_sleep_ms(20);

    TF_CHECK(s_inherit_mutex.owner == &s_inherit_low);
    TF_CHECK_INT(s_inherit_low.base_priority, 5);
    TF_CHECK_INT(s_inherit_low.priority, 2);          /* 被提升到等待者优先级 */

    /* 等低优先级任务解锁并回落 */
    iot_sleep_ms(100);

    TF_CHECK(s_inherit_mutex.owner == NULL);
    TF_CHECK_INT(s_inherit_low.priority, 5);          /* 恢复原始优先级 */

    tf_end();
}

/* ============================== 对外入口 ============================== */

void iot_test_mutex(void)
{
    test_mutex_basic();
    test_mutex_owner();
    test_mutex_mutual_exclusion();
    test_priority_inherit();
}
