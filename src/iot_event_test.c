/*
 * iot_event_test.c - 事件组测试（对应 iot_event.c）
 *
 * 覆盖：ANY/ALL 等待、CLEAR 语义、事件位读取。
 */

#include "test.h"
#include "iot.h"

static iot_event_t s_event;

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

void iot_test_event(void)
{
    test_event();
}
