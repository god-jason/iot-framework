/*
 * iot_queue_test.c - 消息队列测试（对应 iot_queue.c）
 *
 * 覆盖：初始化校验、FIFO、满/空超时、阻塞收发、发送阻塞超时、
 *       FromISR 非阻塞、生产消费综合。
 */

#include "test.h"
#include "iot.h"

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

static volatile uint32_t s_total;
static volatile uint32_t s_count;

static void producer_entry(void *arg)
{
    uint32_t i;

    (void)arg;
    for (i = 1U; i <= 8U; i++) {
        (void)iot_queue_send(&s_q, &i, 200U);
    }
}

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

static void consumer_entry(void *arg)
{
    uint32_t i;
    uint32_t v;

    (void)arg;
    for (i = 0U; i < 10U; i++) {
        v = 0U;
        if (iot_queue_recv(&s_q, &v, 1000U) == IOT_OK) {
            s_total += v;
            s_count++;
        }
    }
    (void)iot_sem_give(&s_done);
}

static void test_queue_init(void)
{
    uint32_t v = 0U;

    tf_begin("iot/队列-初始化与 FIFO");

    TF_CHECK_INT(iot_queue_init(NULL, s_qbuf, 4U, 3U), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_queue_init(&s_q, NULL, 4U, 3U), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, 0U, 3U), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, 4U, 0U), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, sizeof(uint32_t), 3U), IOT_OK);
    TF_CHECK_INT(s_q.depth, 3);

    v = 1U; TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    v = 2U; TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    v = 3U; TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    /* 满：非阻塞发送立即失败 */
    v = 4U; TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    v = 0U;
    TF_CHECK_INT(iot_queue_recv(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(v, 1U);
    TF_CHECK_INT(iot_queue_recv(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(v, 2U);
    TF_CHECK_INT(iot_queue_recv(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(v, 3U);
    /* 空：非阻塞接收立即失败 */
    TF_CHECK_INT(iot_queue_recv(&s_q, &v, IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    tf_end();
}

static void test_queue_invalid(void)
{
    uint32_t v = 0U;
    iot_queue_t bad;

    tf_begin("iot/队列-非法参数");

    memset(&bad, 0, sizeof(bad));
    TF_CHECK_INT(iot_queue_send(&bad, &v, IOT_TIMEOUT_NONE), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_queue_recv(&bad, &v, IOT_TIMEOUT_NONE), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_queue_send(&s_q, NULL, IOT_TIMEOUT_NONE), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_queue_recv(&s_q, NULL, IOT_TIMEOUT_NONE), IOT_INVALID_PARAM);

    tf_end();
}

static void test_queue_blocking(void)
{
    uint32_t v;
    int i;

    tf_begin("iot/队列-阻塞收发");

    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, sizeof(uint32_t), 2U), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_producer, "producer", 2, producer_entry,
                                 NULL, s_producer_stack,
                                 sizeof(s_producer_stack)), IOT_OK);

    for (i = 1; i <= 8; i++) {
        v = 0U;
        TF_CHECK_INT(iot_queue_recv(&s_q, &v, 200U), IOT_OK);
        TF_CHECK_INT(v, (uint32_t)i);
    }

    tf_end();
}

static void test_queue_send_timeout(void)
{
    uint32_t v = 0U;
    uint32_t t0;

    tf_begin("iot/队列-发送超时");

    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, sizeof(uint32_t), 1U), IOT_OK);
    TF_CHECK_INT(iot_queue_send(&s_q, &v, IOT_TIMEOUT_NONE), IOT_OK);

    t0 = iot_tick_get();
    v = 1U;
    TF_CHECK_INT(iot_queue_send(&s_q, &v, 5U), IOT_TIMEOUT);
    TF_CHECK((iot_tick_get() - t0) >= 5U);

    tf_end();
}

static void test_queue_from_isr(void)
{
    uint32_t msg = 0x1234U;
    uint32_t out = 0U;
    uint32_t i;

    tf_begin("iot/队列-FromISR");

    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, sizeof(uint32_t), 4U), IOT_OK);
    TF_CHECK_INT(iot_queue_send_from_isr(&s_q, &msg, NULL), IOT_OK);
    TF_CHECK_INT(iot_queue_recv(&s_q, &out, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(out, 0x1234);

    for (i = 0U; i < 4U; i++) {
        TF_CHECK_INT(iot_queue_send_from_isr(&s_q, &msg, NULL), IOT_OK);
    }
    /* 满时绝不阻塞 */
    TF_CHECK_INT(iot_queue_send_from_isr(&s_q, &msg, NULL), IOT_TIMEOUT);

    tf_end();
}

static void test_queue_multi_task(void)
{
    tf_begin("iot/队列-生产消费综合");

    s_total = 0U;
    s_count = 0U;

    TF_CHECK_INT(iot_queue_init(&s_q, s_qbuf, sizeof(uint32_t), 2U), IOT_OK);
    TF_CHECK_INT(iot_sem_init(&s_done, 0U), IOT_OK);

    TF_CHECK_INT(iot_task_create(&s_consumer, "consumer", 2, consumer_entry,
                                 NULL, s_consumer_stack,
                                 sizeof(s_consumer_stack)), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_prod1, "prod1", 3, prod1_entry, NULL,
                                 s_prod_stack1, sizeof(s_prod_stack1)), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_prod2, "prod2", 3, prod2_entry, NULL,
                                 s_prod_stack2, sizeof(s_prod_stack2)), IOT_OK);

    TF_CHECK_INT(iot_sem_take(&s_done, 3000U), IOT_OK);
    TF_CHECK_INT((int)s_count, 10);
    TF_CHECK_INT((int)s_total, 30);      /* (1+2+3+4+5) * 2 */

    tf_end();
}

void iot_test_queue(void)
{
    test_queue_init();
    test_queue_invalid();
    test_queue_blocking();
    test_queue_send_timeout();
    test_queue_from_isr();
    test_queue_multi_task();
}
