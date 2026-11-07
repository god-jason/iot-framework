/* 容器测试：侵入式链表、定长队列、环形缓冲、固定内存池。 */
#include "test_framework.h"
#include "iot_list.h"
#include "iot_queue.h"
#include "iot_ringbuf.h"
#include "iot_pool.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    int value;
    iot_list_node_t node;
} list_item_t;

static void test_list(void)
{
    iot_list_t list;
    list_item_t a, b, c;
    iot_list_node_t *n;
    uint32_t expect[3] = { 0, 0, 0 };
    uint32_t i = 0;

    a.value = 1;
    b.value = 2;
    c.value = 3;

    tf_begin("container/list 添加与顺序");
    iot_list_init(&list);
    iot_list_node_init(&a.node);
    iot_list_node_init(&b.node);
    iot_list_node_init(&c.node);

    TF_CHECK(iot_list_empty(&list));
    iot_list_add(&list, &a.node);        /* 头部插入：A */
    iot_list_add(&list, &b.node);        /* 头部插入：B,A */
    iot_list_add_tail(&list, &c.node);   /* 尾部插入：B,A,C */
    TF_CHECK(!iot_list_empty(&list));
    TF_CHECK_INT(iot_list_size(&list), 3);

    for (n = iot_list_first(&list); n != &list.head; n = iot_list_next(n)) {
        list_item_t *it = IOT_CONTAINER_OF(n, list_item_t, node);
        if (i < 3) {
            expect[i] = (uint32_t)it->value;
        }
        i++;
    }
    TF_CHECK_INT(expect[0], 2);
    TF_CHECK_INT(expect[1], 1);
    TF_CHECK_INT(expect[2], 3);
    tf_end();

    tf_begin("container/list 移除");
    iot_list_remove(&a.node);
    TF_CHECK_INT(iot_list_size(&list), 2);
    TF_CHECK(iot_list_first(&list) != NULL);
    iot_list_remove(&b.node);
    iot_list_remove(&c.node);
    TF_CHECK(iot_list_empty(&list));
    TF_CHECK(iot_list_first(&list) == NULL);
    tf_end();
}

static void test_queue(void)
{
    iot_queue_t q;
    int buffer[4];
    int v = 0;

    tf_begin("container/queue 基本操作");
    TF_CHECK_INT(iot_queue_init(&q, buffer, sizeof(int), 4), IOT_OK);
    TF_CHECK(iot_queue_empty(&q));
    TF_CHECK(!iot_queue_full(&q));

    TF_CHECK_INT(iot_queue_push(&q, &(int){ 10 }), IOT_OK);
    TF_CHECK_INT(iot_queue_push(&q, &(int){ 20 }), IOT_OK);
    TF_CHECK_INT(iot_queue_push(&q, &(int){ 30 }), IOT_OK);
    TF_CHECK_INT(iot_queue_push(&q, &(int){ 40 }), IOT_OK);
    TF_CHECK(iot_queue_full(&q));
    TF_CHECK_INT(iot_queue_size(&q), 4);
    TF_CHECK_INT(iot_queue_push(&q, &(int){ 50 }), -1);

    TF_CHECK_INT(iot_queue_peek(&q, &v), IOT_OK);
    TF_CHECK_INT(v, 10);
    TF_CHECK_INT(iot_queue_pop(&q, &v), IOT_OK);
    TF_CHECK_INT(v, 10);
    TF_CHECK_INT(iot_queue_pop(&q, &v), IOT_OK);
    TF_CHECK_INT(v, 20);
    TF_CHECK_INT(iot_queue_size(&q), 2);
    tf_end();

    tf_begin("container/queue 清空与下溢");
    iot_queue_clear(&q);
    TF_CHECK(iot_queue_empty(&q));
    TF_CHECK_INT(iot_queue_pop(&q, &v), -1);
    TF_CHECK_INT(iot_queue_peek(&q, &v), -1);
    TF_CHECK_INT(iot_queue_init(NULL, buffer, sizeof(int), 4), -1);
    tf_end();
}

static void test_ringbuf(void)
{
    iot_ringbuf_t rb;
    uint8_t buffer[8];
    uint8_t out[16];

    tf_begin("container/ringbuf 写入与容量");
    TF_CHECK_INT(iot_ringbuf_init(&rb, buffer, sizeof(buffer)), IOT_OK);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 0);
    TF_CHECK_INT(iot_ringbuf_free(&rb), 8);

    TF_CHECK_INT(iot_ringbuf_write(&rb, "hello", 5), 5);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 5);
    TF_CHECK_INT(iot_ringbuf_free(&rb), 3);

    /* 只接受剩余 3 字节，返回实际写入量 */
    TF_CHECK_INT(iot_ringbuf_write(&rb, "XYZ12", 5), 3);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 8);
    TF_CHECK_INT(iot_ringbuf_free(&rb), 0);
    tf_end();

    tf_begin("container/ringbuf 读取与环绕");
    TF_CHECK_INT(iot_ringbuf_peek(&rb, out, 8), 8);
    TF_CHECK_MEM(out, "helloXYZ", 8);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 8);

    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 8), 8);
    TF_CHECK_MEM(out, "helloXYZ", 8);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 0);

    /* 写入位置已环绕到索引 0 */
    TF_CHECK_INT(iot_ringbuf_write(&rb, "abc", 3), 3);
    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 3), 3);
    TF_CHECK_MEM(out, "abc", 3);
    tf_end();

    tf_begin("container/ringbuf skip 与 reset");
    iot_ringbuf_write(&rb, "01234567", 8);
    TF_CHECK_INT(iot_ringbuf_skip(&rb, 3), 3);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 5);
    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 5), 5);
    TF_CHECK_MEM(out, "34567", 5);

    iot_ringbuf_reset(&rb);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 0);
    TF_CHECK_INT(iot_ringbuf_init(NULL, buffer, 8), -1);
    tf_end();
}

static void test_pool(void)
{
    iot_pool_t pool;
    /* 3 块 * 16 字节 = 48 字节，uint64_t 保证指针对齐 */
    uint64_t storage[6];
    void *p1, *p2, *p3, *p4;

    tf_begin("container/pool 分配与释放");
    TF_CHECK_INT(iot_pool_init(&pool, storage, 16, 3), IOT_OK);
    TF_CHECK_INT(iot_pool_available(&pool), 3);
    TF_CHECK_INT(iot_pool_used(&pool), 0);

    p1 = iot_pool_alloc(&pool);
    p2 = iot_pool_alloc(&pool);
    p3 = iot_pool_alloc(&pool);
    TF_CHECK(p1 != NULL && p2 != NULL && p3 != NULL);
    TF_CHECK(p1 != p2 && p2 != p3 && p1 != p3);
    TF_CHECK_INT(iot_pool_available(&pool), 0);
    TF_CHECK_INT(iot_pool_used(&pool), 3);

    p4 = iot_pool_alloc(&pool);
    TF_CHECK(p4 == NULL);

    TF_CHECK_INT(iot_pool_free(&pool, p2), IOT_OK);
    TF_CHECK_INT(iot_pool_available(&pool), 1);
    p4 = iot_pool_alloc(&pool);
    TF_CHECK(p4 != NULL);
    TF_CHECK(p4 == p2);
    tf_end();

    tf_begin("container/pool 参数校验");
    TF_CHECK_INT(iot_pool_init(NULL, storage, 16, 3), -1);
    TF_CHECK_INT(iot_pool_free(&pool, NULL), -1);
    tf_end();
}

void test_container(void)
{
    test_list();
    test_queue();
    test_ringbuf();
    test_pool();
}
