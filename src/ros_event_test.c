/*
 * ros_event_test.c - 事件组测试（对应 ros_event.c）
 *
 * 覆盖：ANY/ALL 等待、CLEAR 语义、手动清位、参数校验、阻塞唤醒。
 */

#include "test.h"
#include "ros.h"

static ros_event_t s_event;
static ros_task_t s_setter;
static uint8_t s_setter_stack[2048];

static void setter_entry(void *arg)
{
    (void)arg;
    ros_sleep_ms(2);
    (void)ros_event_set(&s_event, 0x04U);
}

static void test_event_basic(void)
{
    uint32_t flags = 0xFFFFU;

    tf_begin("ros/事件组-基本操作");

    TF_CHECK_INT(ros_event_init(NULL), ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_event_init(&s_event), ROS_OK);
    TF_CHECK_INT(ros_event_get(&s_event, &flags), ROS_OK);
    TF_CHECK_INT(flags, 0);

    TF_CHECK_INT(ros_event_set(&s_event, 0x03U), ROS_OK);
    TF_CHECK_INT(ros_event_get(&s_event, &flags), ROS_OK);
    TF_CHECK_INT(flags, 0x03);

    TF_CHECK_INT(ros_event_clear(&s_event, 0x01U), ROS_OK);
    TF_CHECK_INT(ros_event_get(&s_event, &flags), ROS_OK);
    TF_CHECK_INT(flags, 0x02);

    TF_CHECK_INT(ros_event_set(NULL, 1U), ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_event_get(NULL, &flags), ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_event_get(&s_event, NULL), ROS_INVALID_PARAM);

    tf_end();
}

static void test_event_wait(void)
{
    uint32_t flags = 0U;

    tf_begin("ros/事件组-等待语义");

    TF_CHECK_INT(ros_event_init(&s_event), ROS_OK);
    TF_CHECK_INT(ros_event_set(&s_event, 0x03U), ROS_OK);

    /* ANY：任一位满足 */
    TF_CHECK_INT(ros_event_wait(&s_event, 0x01U, ROS_EVENT_ANY,
                                ROS_TIMEOUT_NONE, &flags), ROS_OK);
    TF_CHECK_INT(flags, 0x03);

    /* ALL：缺少位则超时 */
    TF_CHECK_INT(ros_event_wait(&s_event, 0x05U, ROS_EVENT_ALL,
                                ROS_TIMEOUT_NONE, &flags), ROS_TIMEOUT);

    /* CLEAR：满足后清位 */
    TF_CHECK_INT(ros_event_wait(&s_event, 0x03U,
                                ROS_EVENT_ALL | ROS_EVENT_CLEAR,
                                ROS_TIMEOUT_NONE, &flags), ROS_OK);
    TF_CHECK_INT(ros_event_get(&s_event, &flags), ROS_OK);
    TF_CHECK_INT(flags, 0);

    /* 非法参数 */
    TF_CHECK_INT(ros_event_wait(&s_event, 0U, ROS_EVENT_ANY,
                                ROS_TIMEOUT_NONE, &flags), ROS_INVALID_PARAM);
    TF_CHECK_INT(ros_event_wait(NULL, 1U, ROS_EVENT_ANY,
                                ROS_TIMEOUT_NONE, &flags), ROS_INVALID_PARAM);

    tf_end();
}

static void test_event_block(void)
{
    uint32_t flags = 0U;
    uint32_t t0;

    tf_begin("ros/事件组-阻塞唤醒");

    TF_CHECK_INT(ros_event_init(&s_event), ROS_OK);
    TF_CHECK_INT(ros_task_create(&s_setter, "setter", 2, setter_entry, NULL,
                                 s_setter_stack, sizeof(s_setter_stack)),
                 ROS_OK);

    t0 = ros_tick_get();
    TF_CHECK_INT(ros_event_wait(&s_event, 0x04U, ROS_EVENT_ALL, 100U, &flags),
                 ROS_OK);
    TF_CHECK((ros_tick_get() - t0) >= 2U);
    TF_CHECK_INT(flags & 0x04U, 0x04);

    tf_end();
}

static void test_event_timeout(void)
{
    uint32_t t0;

    tf_begin("ros/事件组-等待超时");

    TF_CHECK_INT(ros_event_init(&s_event), ROS_OK);
    t0 = ros_tick_get();
    TF_CHECK_INT(ros_event_wait(&s_event, 0x01U, ROS_EVENT_ANY, 5U, NULL),
                 ROS_TIMEOUT);
    TF_CHECK((ros_tick_get() - t0) >= 5U);

    tf_end();
}

static void test_event_from_isr(void)
{
    uint32_t flags = 0U;
    bool need_yield = true;

    tf_begin("ros/事件组-FromISR");

    TF_CHECK_INT(ros_event_init(&s_event), ROS_OK);
    TF_CHECK_INT(ros_event_set_from_isr(&s_event, 0x5U, &need_yield), ROS_OK);
    TF_CHECK_INT(ros_event_get(&s_event, &flags), ROS_OK);
    TF_CHECK_INT(flags, 0x5);
    TF_CHECK_INT(ros_event_set_from_isr(NULL, 1U, NULL), ROS_INVALID_PARAM);

    tf_end();
}

void ros_test_event(void)
{
    test_event_basic();
    test_event_wait();
    test_event_block();
    test_event_timeout();
    test_event_from_isr();
}
