/*
 * iot_test_ext.c - 内核扩展特性测试
 *
 * 覆盖 P0/P1/P2 新增能力：
 *   - 互斥量优先级继承（含解锁后恢复原始优先级）；
 *   - 事件组（ANY/ALL/CLEAR/超时）；
 *   - 任务通知（发送、消费、空读超时）；
 *   - 运行时统计（切换次数、tick、栈余量）；
 *   - 优先级动态调整。
 */

#include "test.h"
#include "iot.h"

static iot_event_t s_event;
static iot_mutex_t s_mtx;
static iot_sem_t s_locked;

static iot_task_t s_low;
static iot_task_t s_high;
static uint8_t s_low_stack[4096];
static uint8_t s_high_stack[4096];

/* 低优先级任务：持锁后睡眠，制造优先级反转场景 */
static void low_entry(void *arg)
{
    (void)arg;

    (void)iot_mutex_lock(&s_mtx, IOT_TIMEOUT_NONE);
    (void)iot_sem_give(&s_locked);

    iot_sleep_ms(50);
    (void)iot_mutex_unlock(&s_mtx);

    for (;;) {
        iot_sleep_ms(1000);
    }
}

/* 高优先级任务：等低优先级持锁后尝试加锁，触发优先级继承 */
static void high_entry(void *arg)
{
    (void)arg;

    iot_sleep_ms(10);
    (void)iot_mutex_lock(&s_mtx, IOT_TIMEOUT_NONE);   /* 立即超时，但已触发继承 */

    for (;;) {
        iot_sleep_ms(1000);
    }
}

static void test_priority_inherit(void)
{
    tf_begin("iot/优先级继承");

    TF_CHECK_INT(iot_mutex_init(&s_mtx), IOT_OK);
    TF_CHECK_INT(iot_sem_init(&s_locked, 0U), IOT_OK);

    TF_CHECK_INT(iot_task_create(&s_low, "low", 5U, low_entry, NULL,
                                 s_low_stack, sizeof(s_low_stack)), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_high, "high", 2U, high_entry, NULL,
                                 s_high_stack, sizeof(s_high_stack)), IOT_OK);

    /* 等低优先级任务拿到锁 */
    TF_CHECK_INT(iot_sem_take(&s_locked, IOT_TIMEOUT_FOREVER), IOT_OK);

    /* 给高优先级任务一次加锁机会 */
    iot_sleep_ms(20);

    TF_CHECK(s_mtx.owner == &s_low);
    TF_CHECK_INT(s_low.base_priority, 5);
    TF_CHECK_INT(s_low.priority, 2);          /* 被提升到等待者优先级 */

    /* 等低优先级任务解锁并回落 */
    iot_sleep_ms(100);

    TF_CHECK(s_mtx.owner == NULL);
    TF_CHECK_INT(s_low.priority, 5);          /* 恢复原始优先级 */

    tf_end();
}

static void test_event(void)
{
    uint32_t flags = 0U;

    tf_begin("iot/事件组");

    TF_CHECK_INT(iot_event_init(&s_event), IOT_OK);
    TF_CHECK_INT(iot_event_set(&s_event, 0x03U), IOT_OK);

    TF_CHECK_INT(iot_event_wait(&s_event, 0x01U, IOT_EVENT_ANY,
                                IOT_TIMEOUT_NONE, &flags), IOT_OK);
    TF_CHECK_INT(flags, 0x03);

    TF_CHECK_INT(iot_event_wait(&s_event, 0x05U, IOT_EVENT_ALL,
                                IOT_TIMEOUT_NONE, &flags), IOT_TIMEOUT);

    TF_CHECK_INT(iot_event_wait(&s_event, 0x03U, IOT_EVENT_ALL | IOT_EVENT_CLEAR,
                                IOT_TIMEOUT_NONE, &flags), IOT_OK);

    TF_CHECK_INT(iot_event_get(&s_event, &flags), IOT_OK);
    TF_CHECK_INT(flags, 0);

    tf_end();
}

static void test_notify(void)
{
    iot_task_t *self = iot_current();
    uint32_t value = 0U;

    tf_begin("iot/任务通知");

    TF_CHECK_INT(iot_task_notify(self, 0x5A5AU), IOT_OK);
    TF_CHECK_INT(iot_task_wait_notify(self, IOT_TIMEOUT_NONE, &value), IOT_OK);
    TF_CHECK_INT(value, 0x5A5A);

    /* 已消费：再次等待应立即超时 */
    TF_CHECK_INT(iot_task_wait_notify(self, IOT_TIMEOUT_NONE, &value), IOT_TIMEOUT);

    tf_end();
}

static void test_stats(void)
{
    iot_stats_t stats;
    iot_task_stats_t task_stats;

    tf_begin("iot/运行时统计");

    TF_CHECK_INT(iot_stats_get(&stats), IOT_OK);
    TF_CHECK(stats.ctx_switches > 0U);
    TF_CHECK(stats.tick_count > 0U);

    TF_CHECK_INT(iot_task_get_stats(iot_current(), &task_stats), IOT_OK);
    TF_CHECK(task_stats.stack_free_bytes > 0U);

    iot_stats_reset();
    TF_CHECK_INT(iot_stats_get(&stats), IOT_OK);
    TF_CHECK_INT(stats.ctx_switches, 0);

    tf_end();
}

static void test_priority_set(void)
{
    tf_begin("iot/优先级调整");

    TF_CHECK_INT(iot_task_set_priority(&s_low, 8U), IOT_OK);
    TF_CHECK_INT(s_low.priority, 8);
    TF_CHECK_INT(s_low.base_priority, 8);

    /* 空闲优先级保留，不允许设置 */
    TF_CHECK_INT(iot_task_set_priority(&s_low, IOT_PRIORITY_IDLE), IOT_INVALID_PARAM);

    TF_CHECK_INT(iot_task_set_priority(&s_low, 5U), IOT_OK);
    TF_CHECK_INT(s_low.priority, 5);

    tf_end();
}

void iot_test_ext(void)
{
    test_priority_inherit();
    test_event();
    test_notify();
    test_stats();
    test_priority_set();
}
