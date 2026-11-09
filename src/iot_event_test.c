/*
 * iot_event_test.c - 事件组测试（对应 iot_event.c）
 *
 * 覆盖：ANY/ALL 等待、CLEAR 语义、手动清位、参数校验、阻塞唤醒。
 */

#include "test.h"
#include "iot.h"

static iot_event_t s_event;
static iot_task_t s_setter;
static uint8_t s_setter_stack[2048];

static void setter_entry(void *arg)
{
    (void)arg;
    iot_sleep_ms(2);
    (void)iot_event_set(&s_event, 0x04U);
}

static void test_event_basic(void)
{
    uint32_t flags = 0xFFFFU;

    tf_begin("iot/事件组-基本操作");

    TF_CHECK_INT(iot_event_init(NULL), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_event_init(&s_event), IOT_OK);
    TF_CHECK_INT(iot_event_get(&s_event, &flags), IOT_OK);
    TF_CHECK_INT(flags, 0);

    TF_CHECK_INT(iot_event_set(&s_event, 0x03U), IOT_OK);
    TF_CHECK_INT(iot_event_get(&s_event, &flags), IOT_OK);
    TF_CHECK_INT(flags, 0x03);

    TF_CHECK_INT(iot_event_clear(&s_event, 0x01U), IOT_OK);
    TF_CHECK_INT(iot_event_get(&s_event, &flags), IOT_OK);
    TF_CHECK_INT(flags, 0x02);

    TF_CHECK_INT(iot_event_set(NULL, 1U), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_event_get(NULL, &flags), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_event_get(&s_event, NULL), IOT_INVALID_PARAM);

    tf_end();
}

static void test_event_wait(void)
{
    uint32_t flags = 0U;

    tf_begin("iot/事件组-等待语义");

    TF_CHECK_INT(iot_event_init(&s_event), IOT_OK);
    TF_CHECK_INT(iot_event_set(&s_event, 0x03U), IOT_OK);

    /* ANY：任一位满足 */
    TF_CHECK_INT(iot_event_wait(&s_event, 0x01U, IOT_EVENT_ANY,
                                IOT_TIMEOUT_NONE, &flags), IOT_OK);
    TF_CHECK_INT(flags, 0x03);

    /* ALL：缺少位则超时 */
    TF_CHECK_INT(iot_event_wait(&s_event, 0x05U, IOT_EVENT_ALL,
                                IOT_TIMEOUT_NONE, &flags), IOT_TIMEOUT);

    /* CLEAR：满足后清位 */
    TF_CHECK_INT(iot_event_wait(&s_event, 0x03U,
                                IOT_EVENT_ALL | IOT_EVENT_CLEAR,
                                IOT_TIMEOUT_NONE, &flags), IOT_OK);
    TF_CHECK_INT(iot_event_get(&s_event, &flags), IOT_OK);
    TF_CHECK_INT(flags, 0);

    /* 非法参数 */
    TF_CHECK_INT(iot_event_wait(&s_event, 0U, IOT_EVENT_ANY,
                                IOT_TIMEOUT_NONE, &flags), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_event_wait(NULL, 1U, IOT_EVENT_ANY,
                                IOT_TIMEOUT_NONE, &flags), IOT_INVALID_PARAM);

    tf_end();
}

static void test_event_block(void)
{
    uint32_t flags = 0U;
    uint32_t t0;

    tf_begin("iot/事件组-阻塞唤醒");

    TF_CHECK_INT(iot_event_init(&s_event), IOT_OK);
    TF_CHECK_INT(iot_task_create(&s_setter, "setter", 2, setter_entry, NULL,
                                 s_setter_stack, sizeof(s_setter_stack)),
                 IOT_OK);

    t0 = iot_tick_get();
    TF_CHECK_INT(iot_event_wait(&s_event, 0x04U, IOT_EVENT_ALL, 100U, &flags),
                 IOT_OK);
    TF_CHECK((iot_tick_get() - t0) >= 2U);
    TF_CHECK_INT(flags & 0x04U, 0x04);

    tf_end();
}

static void test_event_timeout(void)
{
    uint32_t t0;

    tf_begin("iot/事件组-等待超时");

    TF_CHECK_INT(iot_event_init(&s_event), IOT_OK);
    t0 = iot_tick_get();
    TF_CHECK_INT(iot_event_wait(&s_event, 0x01U, IOT_EVENT_ANY, 5U, NULL),
                 IOT_TIMEOUT);
    TF_CHECK((iot_tick_get() - t0) >= 5U);

    tf_end();
}

static void test_event_from_isr(void)
{
    uint32_t flags = 0U;
    bool need_yield = true;

    tf_begin("iot/事件组-FromISR");

    TF_CHECK_INT(iot_event_init(&s_event), IOT_OK);
    TF_CHECK_INT(iot_event_set_from_isr(&s_event, 0x5U, &need_yield), IOT_OK);
    TF_CHECK_INT(iot_event_get(&s_event, &flags), IOT_OK);
    TF_CHECK_INT(flags, 0x5);
    TF_CHECK_INT(iot_event_set_from_isr(NULL, 1U, NULL), IOT_INVALID_PARAM);

    tf_end();
}

void iot_test_event(void)
{
    test_event_basic();
    test_event_wait();
    test_event_block();
    test_event_timeout();
    test_event_from_isr();
}
