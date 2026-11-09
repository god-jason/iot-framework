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

/* O(1) 等待队列：位图选优、同优先级 FIFO、继承提升后仍能正确摘除 */
static void test_wait_queue(void)
{
    iot_wait_queue_t queue;
    iot_task_t a;
    iot_task_t b;
    iot_task_t c;

    tf_begin("iot/O(1)等待队列");

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    list_node_init(&a.wait_node);
    list_node_init(&b.wait_node);
    list_node_init(&c.wait_node);
    a.priority = 7U;
    b.priority = 2U;
    c.priority = 7U;

    iot_wait_queue_init(&queue);
    TF_CHECK_INT(iot_wait_queue_top_priority(&queue), 0xFF);
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == NULL);

    iot_wait_queue_add(&queue, &a);
    iot_wait_queue_add(&queue, &b);
    iot_wait_queue_add(&queue, &c);

    TF_CHECK_INT(iot_wait_queue_top_priority(&queue), 2);
    TF_CHECK(iot_wait_queue_contains(&queue, &a));
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == &b);   /* 优先级最高 */
    TF_CHECK(!iot_wait_queue_contains(&queue, &b));
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == &a);   /* 同优先级 FIFO */
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == &c);
    TF_CHECK_INT(queue.bitmap, 0);
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == NULL);

    /* 挂入后被优先级继承提升：摘除仍应回到挂入时的链 */
    iot_wait_queue_add(&queue, &a);
    iot_wait_queue_add(&queue, &b);
    a.priority = 1U;
    iot_wait_queue_remove(&queue, &a);
    TF_CHECK_INT(queue.bitmap, (1U << 2));
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == &b);
    TF_CHECK_INT(queue.bitmap, 0);

    tf_end();
}

/* FromISR 接口：只唤醒不阻塞，need_yield 反映抢占需求 */
static void test_from_isr(void)
{
    iot_sem_t sem;
    iot_queue_t queue;
    uint8_t qbuf[4U * sizeof(uint32_t)];
    uint32_t msg = 0x1234U;
    uint32_t out = 0U;
    bool need_yield = true;
    uint32_t i;

    tf_begin("iot/FromISR接口");

    /* PC 模拟端全部逻辑都在任务上下文 */
    TF_CHECK(!iot_in_isr());

    TF_CHECK_INT(iot_sem_init(&sem, 0U), IOT_OK);
    TF_CHECK_INT(iot_sem_give_from_isr(&sem, &need_yield), IOT_OK);
    TF_CHECK(need_yield == iot_sched_preempt_needed());
    TF_CHECK_INT(iot_sem_take(&sem, IOT_TIMEOUT_NONE), IOT_OK);

    TF_CHECK_INT(iot_queue_init(&queue, qbuf,
                                (uint32_t)sizeof(uint32_t), 4U), IOT_OK);
    TF_CHECK_INT(iot_queue_send_from_isr(&queue, &msg, NULL), IOT_OK);
    TF_CHECK_INT(iot_queue_recv(&queue, &out, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(out, 0x1234);

    /* 填满后 FromISR 必须立即返回，绝不阻塞 */
    for (i = 0U; i < 4U; i++) {
        TF_CHECK_INT(iot_queue_send_from_isr(&queue, &msg, NULL), IOT_OK);
    }
    TF_CHECK_INT(iot_queue_send_from_isr(&queue, &msg, NULL), IOT_TIMEOUT);

#if IOT_USE_EVENT
    {
        iot_event_t event;
        uint32_t flags = 0U;

        TF_CHECK_INT(iot_event_init(&event), IOT_OK);
        TF_CHECK_INT(iot_event_set_from_isr(&event, 0x5U, NULL), IOT_OK);
        TF_CHECK_INT(iot_event_get(&event, &flags), IOT_OK);
        TF_CHECK_INT(flags, 0x5);
    }
#endif

#if IOT_USE_NOTIFY
    {
        uint32_t value = 0U;

        TF_CHECK_INT(iot_task_notify_from_isr(iot_current(), 0x77U, NULL), IOT_OK);
        TF_CHECK_INT(iot_task_wait_notify(iot_current(),
                                          IOT_TIMEOUT_NONE, &value), IOT_OK);
        TF_CHECK_INT(value, 0x77);
    }
#endif

    tf_end();
}

/* 整栈魔数填充后，栈水位线应反映真实可用空间 */
static void test_stack_watermark(void)
{
    iot_task_stats_t stats;

    tf_begin("iot/栈水位线");

    TF_CHECK_INT(iot_task_get_stats(iot_current(), &stats), IOT_OK);
    TF_CHECK(stats.stack_free_bytes >= 4096U);

    iot_stats_reset();
    TF_CHECK_INT(iot_task_get_stats(iot_current(), &stats), IOT_OK);
    TF_CHECK_INT(stats.run_ticks, 0);
    TF_CHECK_INT(stats.switches, 0);

    tf_end();
}

static void test_preempt_query(void)
{
    tf_begin("iot/抢占判定");

    /* driver 是最高优先级 0，不应有更高优先级任务就绪 */
    TF_CHECK(!iot_sched_preempt_needed());

    tf_end();
}

void iot_test_ext(void)
{
    test_priority_inherit();
    test_event();
    test_notify();
    test_stats();
    test_priority_set();
    test_wait_queue();
    test_from_isr();
    test_stack_watermark();
    test_preempt_query();
}
