/*
 * iot_mutex_test.c - 互斥量测试（对应 iot_mutex.c）
 *
 * 覆盖：基本加解锁、非递归、所有权校验、并发互斥、
 *       优先级继承与解锁后优先级回落。
 */

#include "test.h"
#include "iot.h"

#define MUTEX_ITERS 50

static iot_mutex_t s_mutex;
static iot_sem_t s_done;
static iot_task_t s_helper;
static iot_task_t s_counter_a;
static iot_task_t s_counter_b;
static uint8_t s_helper_stack[2048];
static uint8_t s_counter_stack_a[2048];
static uint8_t s_counter_stack_b[2048];

static volatile uint32_t s_counter;
static volatile int s_helper_result;

static void helper_entry(void *arg)
{
    (void)arg;
    s_helper_result = iot_mutex_unlock(&s_mutex);
}

static void counter_entry(void *arg)
{
    uint32_t i;
    uint32_t tmp;

    (void)arg;
    for (i = 0U; i < MUTEX_ITERS; i++) {
        (void)iot_mutex_lock(&s_mutex, 1000U);
        tmp = s_counter;
        iot_yield();               /* 临界区内让出，制造竞态窗口 */
        s_counter = tmp + 1U;
        (void)iot_mutex_unlock(&s_mutex);
    }
    (void)iot_sem_give(&s_done);
}

static void test_mutex_basic(void)
{
    tf_begin("iot/互斥量-基本与所有权");

    TF_CHECK_INT(iot_mutex_init(NULL), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_mutex_init(&s_mutex), IOT_OK);

    TF_CHECK_INT(iot_mutex_lock(&s_mutex, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK(iot_current() == s_mutex.owner);

    /* 非递归：持有者再次加锁立即超时 */
    TF_CHECK_INT(iot_mutex_lock(&s_mutex, IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    TF_CHECK_INT(iot_mutex_unlock(&s_mutex), IOT_OK);
    TF_CHECK(s_mutex.owner == NULL);

    /* 非持有者解锁 */
    TF_CHECK_INT(iot_mutex_unlock(&s_mutex), IOT_NOT_OWNER);

    tf_end();
}

static void test_mutex_other_owner(void)
{
    tf_begin("iot/互斥量-跨任务所有权");

    TF_CHECK_INT(iot_mutex_init(&s_mutex), IOT_OK);
    TF_CHECK_INT(iot_mutex_lock(&s_mutex, IOT_TIMEOUT_NONE), IOT_OK);

    s_helper_result = 12345;
    TF_CHECK_INT(iot_task_create(&s_helper, "mhelper", 3, helper_entry, NULL,
                                 s_helper_stack, sizeof(s_helper_stack)),
                 IOT_OK);

    iot_sleep_ms(1U);
    TF_CHECK_INT(s_helper_result, IOT_NOT_OWNER);
    TF_CHECK_INT(iot_mutex_unlock(&s_mutex), IOT_OK);

    tf_end();
}

static void test_mutex_exclusion(void)
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

    TF_CHECK_INT(iot_sem_take(&s_done, 2000U), IOT_OK);
    TF_CHECK_INT(iot_sem_take(&s_done, 2000U), IOT_OK);
    TF_CHECK_INT((int)s_counter, 2 * MUTEX_ITERS);

    tf_end();
}

/* ============================== 优先级继承 ============================== */

static iot_mutex_t s_inherit_mutex;
static iot_sem_t s_inherit_locked;
static iot_task_t s_low;
static iot_task_t s_high;
static uint8_t s_low_stack[4096];
static uint8_t s_high_stack[4096];

static void low_entry(void *arg)
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

static void high_entry(void *arg)
{
    (void)arg;

    iot_sleep_ms(10);
    (void)iot_mutex_lock(&s_inherit_mutex, IOT_TIMEOUT_NONE);

    for (;;) {
        iot_sleep_ms(1000);
    }
}

static void test_mutex_inherit(void)
{
    tf_begin("iot/互斥量-优先级继承");

    TF_CHECK_INT(iot_mutex_init(&s_inherit_mutex), IOT_OK);
    TF_CHECK_INT(iot_sem_init(&s_inherit_locked, 0U), IOT_OK);

    TF_CHECK_INT(iot_task_create(&s_low, "low", 5U, low_entry, NULL,
                                 s_low_stack, sizeof(s_low_stack)), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_high, "high", 2U, high_entry, NULL,
                                 s_high_stack, sizeof(s_high_stack)), IOT_OK);

    TF_CHECK_INT(iot_sem_take(&s_inherit_locked, IOT_TIMEOUT_FOREVER), IOT_OK);
    iot_sleep_ms(20);

    TF_CHECK(s_inherit_mutex.owner == &s_low);
    TF_CHECK_INT(s_low.base_priority, 5);
    TF_CHECK_INT(s_low.priority, 2);      /* 被提升到等待者优先级 */

    iot_sleep_ms(100);

    TF_CHECK(s_inherit_mutex.owner == NULL);
    TF_CHECK_INT(s_low.priority, 5);      /* 解锁后回落 */

    tf_end();
}

void iot_test_mutex(void)
{
    test_mutex_basic();
    test_mutex_other_owner();
    test_mutex_exclusion();
    test_mutex_inherit();
}
