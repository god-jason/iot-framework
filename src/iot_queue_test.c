/*
 * iot_queue_test.c - 消息队列测试（对应 iot_queue.c）
 *
 * 覆盖：FIFO、满/空超时、阻塞收发、发送阻塞超时，
 *       以及队列 + 信号量 + 调度的生产消费综合场景。
 */

#include "test.h"
#include "iot.h"

/* ============================== 静态对象 ============================== */

static iot_queue_t s_q;
static uint8_t s_qbuf[4 * sizeof(uint32_t)];
static iot_sem_t s_done;

static iot_task_t s_producer;
static iot_task_t s_prod1;
static iot_task_t s_prod2;
static iot_task_t s_consumer;
static uint8_t s_producer_stack[2048];
static uint8_t s_prod_stack1[2048];
static uint8_t s_prod_stack2[2048];
static uint8_t s_consumer_stack[2048];

static volatile uint32_t s_consumer_total;
static volatile uint32_t s_consumer_count;

/* ============================== 辅助任务 ============================== */

/* 队列阻塞发送：1..8 */
static void producer_entry(void *arg)
{
    uint32_t i;

    (void)arg;
    for (i = 1U; i <= 8U; i++) {
        (void)iot_queue_send(&s_q, &i, 200U);
    }
}

/* 综合场景：两个生产者各发送 1..5 */
static void prod1_entry(void *arg)
{
    uint32_t i;

    (void)arg;
    for (i = 1U; i <= 5U; i++) {
        (void)iot_queue_send(&s_q, &i, 1000U);
    }
}

static void prod2_entry(void *arg)
{
    uint32_t i;

    (void)arg;
    for (i = 1U; i <= 5U; i++) {
        (void)iot_queue_send(&s_q, &i, 1000U);
    }
}

/* 综合场景：消费者接收 10 条并累加 */
static void consumer_entry(void *arg)
{
    uint32_t i;
    uint32_t v;

    (void)arg;
    for (i = 0U; i < 10U; i++) {
        v = 0U;
        if (iot_queue_recv(&s_q, &v, 1000U) == IOT_OK) {
            s_consumer_total += v;
            s_consumer_count++;
        }
    }
    (void)iot_sem_give(&s_done);
}

/* ============================== 单项测试 ============================== */

/* 1. FIFO、满/空超时 */
static void test_queue_basic(void)
{
    uint32_t v;

    tf_begin("iot/队列-基本");

    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, sizeof(uint32_t), 3U), IOT_OK);

    v = 1U; TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    v = 2U; TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    v = 3U; TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    v = 4U; TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    v = 0U;
    TF_CHECK_INT(iot_queue_recv(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(v, 1U);
    TF_CHECK_INT(iot_queue_recv(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(v, 2U);
    TF_CHECK_INT(iot_queue_recv(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(v, 3U);
    TF_CHECK_INT(iot_queue_recv(&s_q, &v, IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    tf_end();
}

/* 2. 小深度阻塞收发（发送满/接收空都会阻塞） */
static void test_queue_blocking(void)
{
    uint32_t v;
    int i;

    tf_begin("iot/队列-阻塞收发");

    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, sizeof(uint32_t), 2U), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_producer, "producer", 2,
                                  producer_entry, NULL,
                                  s_producer_stack, sizeof(s_producer_stack)),
                 IOT_OK);

    for (i = 1; i <= 8; i++) {
        v = 0U;
        TF_CHECK_INT(iot_queue_recv(&s_q, &v, 200U), IOT_OK);
        TF_CHECK_INT(v, (uint32_t)i);
    }

    tf_end();
}

/* 3. 满时阻塞发送超时 */
static void test_queue_send_timeout(void)
{
    uint32_t v;
    uint32_t t0;

    tf_begin("iot/队列-发送超时");

    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, sizeof(uint32_t), 1U), IOT_OK);
    v = 0U;
    TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);

    t0 = iot_tick_get();
    v = 1U;
    TF_CHECK_INT(iot_queue_send(&s_q, &v, 5U), IOT_TIMEOUT);
    TF_CHECK((iot_tick_get() - t0) >= 5U);

    tf_end();
}

/*
 * 4. 多任务综合：
 *   - 两个生产者（prio 3）向深度 2 的队列阻塞发送 1..5；
 *   - 消费者（prio 2）接收 10 条并累加，完成后释放信号量；
 *   - driver（prio 0）等待完成信号量并校验总数。
 */
static void test_multi_task(void)
{
    tf_begin("iot/多任务-生产消费综合");

    s_consumer_total = 0U;
    s_consumer_count = 0U;

    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, sizeof(uint32_t), 2U), IOT_OK);
    TF_CHECK_INT(iot_sem_init(&s_done, 0U), IOT_OK);

    TF_CHECK_INT(iot_task_create(&s_consumer, "consumer", 2,
                                  consumer_entry, NULL,
                                  s_consumer_stack, sizeof(s_consumer_stack)),
                 IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_prod1, "prod1", 3, prod1_entry, NULL,
                                  s_prod_stack1, sizeof(s_prod_stack1)),
                 IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_prod2, "prod2", 3, prod2_entry, NULL,
                                  s_prod_stack2, sizeof(s_prod_stack2)),
                 IOT_OK);

    TF_CHECK_INT(iot_sem_take(&s_done, 3000U), IOT_OK);
    TF_CHECK_INT((int)s_consumer_count, 10);
    TF_CHECK_INT((int)s_consumer_total, 30);   /* (1+2+3+4+5) * 2 */

    tf_end();
}

/* ============================== 对外入口 ============================== */

void iot_test_queue(void)
{
    test_queue_basic();
    test_queue_blocking();
    test_queue_send_timeout();
    test_multi_task();
}
