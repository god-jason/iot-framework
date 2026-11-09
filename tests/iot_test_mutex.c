/*
 * iot_test_mutex.c - 轻量互斥量测试
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

/* ============================== 对外入口 ============================== */

void iot_test_mutex(void)
{
    test_mutex_basic();
    test_mutex_owner();
    test_mutex_mutual_exclusion();
}
