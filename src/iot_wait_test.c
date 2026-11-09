/*
 * iot_wait_test.c - 等待队列测试（对应 iot_wait.c）
 *
 * 覆盖：位图选优、同优先级 FIFO、contains/remove、重复摘除安全、
 *       优先级继承提升后仍能正确摘链。
 */

#include "test.h"
#include "iot.h"

void iot_test_wait(void)
{
    iot_wait_queue_t queue;
    iot_task_t a;
    iot_task_t b;
    iot_task_t c;

    tf_begin("iot/等待队列-位图选优与 FIFO");

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
    TF_CHECK_INT(queue.bitmap, 0);
    TF_CHECK_INT(iot_wait_queue_top_priority(&queue), 0xFF);
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == NULL);

    iot_wait_queue_add(&queue, &a);
    iot_wait_queue_add(&queue, &b);
    iot_wait_queue_add(&queue, &c);

    TF_CHECK_INT(iot_wait_queue_top_priority(&queue), 2);
    TF_CHECK(iot_wait_queue_contains(&queue, &a));
    TF_CHECK(iot_wait_queue_contains(&queue, &b));

    TF_CHECK(iot_wait_queue_pop_highest(&queue) == &b);   /* 优先级最高 */
    TF_CHECK(!iot_wait_queue_contains(&queue, &b));
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == &a);   /* 同优先级 FIFO */
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == &c);
    TF_CHECK_INT(queue.bitmap, 0);
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == NULL);

    tf_end();

    tf_begin("iot/等待队列-摘除与继承提升");

    iot_wait_queue_init(&queue);
    iot_wait_queue_add(&queue, &a);
    iot_wait_queue_add(&queue, &b);

    /* 挂入后优先级被继承提升：摘除仍应回到挂入时的链 */
    a.priority = 1U;
    iot_wait_queue_remove(&queue, &a);
    TF_CHECK_INT(queue.bitmap, (1U << 2));
    TF_CHECK(iot_wait_queue_pop_highest(&queue) == &b);
    TF_CHECK_INT(queue.bitmap, 0);

    /* 对未挂入的任务安全 */
    iot_wait_queue_remove(&queue, &a);
    iot_wait_queue_remove(&queue, NULL);
    iot_wait_queue_remove(NULL, &a);
    TF_CHECK_INT(queue.bitmap, 0);

    /* 全部为空时最高优先级为 0xFF */
    TF_CHECK_INT(iot_wait_queue_top_priority(&queue), 0xFF);

    tf_end();
}
