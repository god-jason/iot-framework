/* src/queue.c per-file 单元测试。 */
#include "test.h"
#include "queue.h"

#include <string.h>

typedef struct {
    int id;
    char tag;
} queue_item_t;

void ut_queue(void)
{
    queue_t q;
    queue_item_t buffer[4];
    queue_item_t in;
    queue_item_t out;

    tf_begin("queue 初始化参数校验");
    memset(buffer, 0, sizeof(buffer));
    TF_CHECK_INT(queue_init(NULL, buffer, sizeof(queue_item_t), 4), -1);
    TF_CHECK_INT(queue_init(&q, NULL, sizeof(queue_item_t), 4), -1);
    TF_CHECK_INT(queue_init(&q, buffer, 0, 4), -1);
    TF_CHECK_INT(queue_init(&q, buffer, sizeof(queue_item_t), 0), -1);
    TF_CHECK_INT(queue_init(&q, buffer, sizeof(queue_item_t), 4), 0);
    TF_CHECK(queue_empty(&q));
    TF_CHECK(!queue_full(&q));
    TF_CHECK_INT(queue_size(&q), 0);
    tf_end();

    tf_begin("queue 空队列出队/查看");
    memset(&out, 0, sizeof(out));
    TF_CHECK_INT(queue_pop(&q, &out), -1);
    TF_CHECK_INT(queue_peek(&q, &out), -1);
    TF_CHECK_INT(queue_pop(&q, NULL), -1);
    TF_CHECK_INT(queue_peek(&q, NULL), -1);
    TF_CHECK_INT(queue_push(NULL, &out), -1);
    TF_CHECK_INT(queue_push(&q, NULL), -1);
    TF_CHECK(queue_empty(NULL));
    TF_CHECK(!queue_full(NULL));
    TF_CHECK_INT(queue_size(NULL), 0);
    queue_clear(NULL);
    tf_end();

    tf_begin("queue 压满与溢出");
    in.id = 1;
    in.tag = 'a';
    TF_CHECK_INT(queue_push(&q, &in), 0);
    in.id = 2;
    TF_CHECK_INT(queue_push(&q, &in), 0);
    in.id = 3;
    TF_CHECK_INT(queue_push(&q, &in), 0);
    in.id = 4;
    TF_CHECK_INT(queue_push(&q, &in), 0);
    TF_CHECK(queue_full(&q));
    TF_CHECK(!queue_empty(&q));
    TF_CHECK_INT(queue_size(&q), 4);

    in.id = 5;
    TF_CHECK_INT(queue_push(&q, &in), -1);  /* 满 */
    TF_CHECK_INT(queue_size(&q), 4);

    TF_CHECK_INT(queue_peek(&q, &out), 0);
    TF_CHECK_INT(out.id, 1);
    TF_CHECK_INT(queue_size(&q), 4);        /* peek 不消费 */
    tf_end();

    tf_begin("queue 环绕复用");
    TF_CHECK_INT(queue_pop(&q, &out), 0);
    TF_CHECK_INT(out.id, 1);
    TF_CHECK_INT(queue_pop(&q, &out), 0);
    TF_CHECK_INT(out.id, 2);

    /* 读指针前移后写入，tail 发生回绕 */
    in.id = 5;
    TF_CHECK_INT(queue_push(&q, &in), 0);
    in.id = 6;
    TF_CHECK_INT(queue_push(&q, &in), 0);
    TF_CHECK_INT(queue_size(&q), 4);

    TF_CHECK_INT(queue_pop(&q, &out), 0);
    TF_CHECK_INT(out.id, 3);
    TF_CHECK_INT(queue_pop(&q, &out), 0);
    TF_CHECK_INT(out.id, 4);
    TF_CHECK_INT(queue_pop(&q, &out), 0);
    TF_CHECK_INT(out.id, 5);
    TF_CHECK_INT(queue_pop(&q, &out), 0);
    TF_CHECK_INT(out.id, 6);
    TF_CHECK_INT(queue_pop(&q, &out), -1);
    TF_CHECK(queue_empty(&q));
    tf_end();

    tf_begin("queue clear 与重新初始化");
    in.id = 7;
    queue_push(&q, &in);
    in.id = 8;
    queue_push(&q, &in);
    TF_CHECK_INT(queue_size(&q), 2);
    queue_clear(&q);
    TF_CHECK(queue_empty(&q));
    TF_CHECK_INT(queue_size(&q), 0);
    TF_CHECK_INT(queue_pop(&q, &out), -1);

    /* 清空后仍可继续使用 */
    in.id = 9;
    TF_CHECK_INT(queue_push(&q, &in), 0);
    TF_CHECK_INT(queue_pop(&q, &out), 0);
    TF_CHECK_INT(out.id, 9);

    /* 重复初始化重置容量与计数 */
    TF_CHECK_INT(queue_init(&q, buffer, sizeof(queue_item_t), 2), 0);
    TF_CHECK_INT(queue_size(&q), 0);
    in.id = 10;
    queue_push(&q, &in);
    in.id = 11;
    queue_push(&q, &in);
    TF_CHECK(queue_full(&q));
    TF_CHECK_INT(queue_push(&q, &in), -1);
    tf_end();
}
