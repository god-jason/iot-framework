/*
 * iot_wait_test.c - 等待队列测试（对应 iot_wait.c）
 *
 * 覆盖：位图选优、同优先级 FIFO、挂入后被继承提升仍能正确摘除。
 */

#include "test.h"
#include "iot.h"

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

void iot_test_wait(void)
{
    test_wait_queue();
}
