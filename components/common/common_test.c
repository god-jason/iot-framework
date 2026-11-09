/*
 * common_test.c - 通用容器测试（list / queue / ringbuf / fsm）
 *
 * 覆盖：双向链表增删与有序插入、定长队列 FIFO、环形缓冲读写与覆盖、
 *       状态机启动/派发/守卫/暂停/停止。
 */

#include "test.h"
#include "list.h"
#include "queue.h"
#include "ringbuf.h"
#include "fsm.h"

/* ============================== list ============================== */

void ut_list(void)
{
    list_t list;
    list_node_t a;
    list_node_t b;
    list_node_t c;
    list_node_t d;

    tf_begin("list/基本增删与遍历");

    list_init(&list);
    TF_CHECK(list_empty(&list));
    TF_CHECK_INT(list_size(&list), 0);
    TF_CHECK(list_first(&list) == NULL);
    TF_CHECK(list_pop_first(&list) == NULL);

    list_node_init(&a);
    list_node_init(&b);
    list_node_init(&c);
    list_add_tail(&list, &a);
    list_add_tail(&list, &b);
    list_add(&list, &c);
    TF_CHECK_INT(list_size(&list), 3);
    TF_CHECK(list_first(&list) == &c);          /* 头插成为首元素 */

    TF_CHECK(list_pop_first(&list) == &c);
    TF_CHECK(list_pop_first(&list) == &a);
    TF_CHECK(list_pop_first(&list) == &b);
    TF_CHECK(list_empty(&list));

    tf_end();

    tf_begin("list/摘除与移动");

    list_init(&list);
    list_node_init(&a);
    list_node_init(&b);
    list_node_init(&c);

    list_add_tail(&list, &a);
    list_add_tail(&list, &b);
    list_add_tail(&list, &c);

    list_remove(&b);
    TF_CHECK_INT(list_size(&list), 2);
    list_remove(&b);                            /* 重复摘除安全 */
    list_remove(NULL);
    TF_CHECK_INT(list_size(&list), 2);

    list_move(&list, &a);                       /* 移到头部 */
    TF_CHECK(list_first(&list) == &a);

    list_insert_before(&b, &d);                 /* b 游离，插入到其前后 */
    list_insert_after(&b, &c);

    tf_end();

    tf_begin("list/按 sort_key 有序插入");

    list_init(&list);
    list_node_init(&a);
    list_node_init(&b);
    list_node_init(&c);
    list_node_init(&d);

    a.sort_key = 30U;
    b.sort_key = 10U;
    c.sort_key = 20U;
    d.sort_key = 20U;                           /* 与 c 同键值 */

    list_insert_sorted(&list, &a);
    list_insert_sorted(&list, &b);
    list_insert_sorted(&list, &c);
    list_insert_sorted(&list, &d);

    TF_CHECK(list_pop_first(&list) == &b);
    TF_CHECK(list_pop_first(&list) == &c);      /* 同键值 FIFO */
    TF_CHECK(list_pop_first(&list) == &d);
    TF_CHECK(list_pop_first(&list) == &a);

    tf_end();

    tf_begin("list/空链表与 NULL 安全");

    TF_CHECK(list_empty(NULL));
    TF_CHECK_INT(list_size(NULL), 0);
    TF_CHECK(list_first(NULL) == NULL);
    list_init(NULL);
    list_add(NULL, &a);
    list_add_tail(&list, NULL);
    list_node_init(NULL);
    list_insert_sorted(NULL, &a);
    TF_CHECK(1);

    tf_end();
}

/* ============================== queue ============================== */

void ut_queue(void)
{
    queue_t q;
    uint8_t buf[3 * sizeof(uint32_t)];
    uint32_t v;

    tf_begin("queue/初始化与 FIFO");

    TF_CHECK_INT(queue_init(NULL, buf, 4U, 3U), -1);
    TF_CHECK_INT(queue_init(&q, NULL, 4U, 3U), -1);
    TF_CHECK_INT(queue_init(&q, buf, 0U, 3U), -1);
    TF_CHECK_INT(queue_init(&q, buf, 4U, 0U), -1);
    TF_CHECK_INT(queue_init(&q, buf, sizeof(uint32_t), 3U), 0);

    TF_CHECK(queue_empty(&q));
    TF_CHECK(!queue_full(&q));
    TF_CHECK_INT(queue_size(&q), 0);

    v = 1U; TF_CHECK_INT(queue_push(&q, &v), 0);
    v = 2U; TF_CHECK_INT(queue_push(&q, &v), 0);
    v = 3U; TF_CHECK_INT(queue_push(&q, &v), 0);
    TF_CHECK(queue_full(&q));
    v = 4U; TF_CHECK_INT(queue_push(&q, &v), -1);

    v = 0U;
    TF_CHECK_INT(queue_peek(&q, &v), 0);
    TF_CHECK_INT(v, 1U);

    TF_CHECK_INT(queue_pop(&q, &v), 0);
    TF_CHECK_INT(v, 1U);
    TF_CHECK_INT(queue_pop(&q, &v), 0);
    TF_CHECK_INT(v, 2U);

    v = 9U; TF_CHECK_INT(queue_push(&q, &v), 0);   /* 环形复用 */
    TF_CHECK_INT(queue_pop(&q, &v), 0);
    TF_CHECK_INT(v, 3U);
    TF_CHECK_INT(queue_pop(&q, &v), 0);
    TF_CHECK_INT(v, 9U);
    TF_CHECK_INT(queue_pop(&q, &v), -1);

    tf_end();

    tf_begin("queue/清空与空判");

    v = 1U; (void)queue_push(&q, &v);
    queue_clear(&q);
    TF_CHECK(queue_empty(&q));
    TF_CHECK_INT(queue_size(&q), 0);
    TF_CHECK(queue_empty(NULL));
    TF_CHECK(!queue_full(NULL));
    TF_CHECK_INT(queue_size(NULL), 0);
    TF_CHECK_INT(queue_pop(NULL, &v), -1);
    TF_CHECK_INT(queue_pop(&q, NULL), -1);

    tf_end();
}

/* ============================== ringbuf ============================== */

void ut_ringbuf(void)
{
    ringbuf_t rb;
    uint8_t buf[8];
    uint8_t out[8];
    const char *msg = "abcdef";

    tf_begin("ringbuf/初始化与读写");

    TF_CHECK_INT(ringbuf_init(NULL, buf, 8U), -1);
    TF_CHECK_INT(ringbuf_init(&rb, NULL, 8U), -1);
    TF_CHECK_INT(ringbuf_init(&rb, buf, 0U), -1);
    TF_CHECK_INT(ringbuf_init(&rb, buf, sizeof(buf)), 0);

    TF_CHECK_INT(ringbuf_available(&rb), 0);
    TF_CHECK_INT(ringbuf_free(&rb), 8);
    TF_CHECK_INT(ringbuf_write(&rb, msg, 6U), 6);
    TF_CHECK_INT(ringbuf_available(&rb), 6);
    TF_CHECK_INT(ringbuf_free(&rb), 2);

    memset(out, 0, sizeof(out));
    TF_CHECK_INT(ringbuf_peek(&rb, out, 4U), 4);
    TF_CHECK_MEM(out, "abcd", 4);
    TF_CHECK_INT(ringbuf_available(&rb), 6);      /* 窥视不移除 */

    TF_CHECK_INT(ringbuf_read(&rb, out, 4U), 4);
    TF_CHECK_MEM(out, "abcd", 4);
    TF_CHECK_INT(ringbuf_available(&rb), 2);
    TF_CHECK_INT(ringbuf_read(&rb, out, 8U), 2);
    TF_CHECK_MEM(out, "ef", 2);
    TF_CHECK_INT(ringbuf_available(&rb), 0);

    tf_end();

    tf_begin("ringbuf/写满与跳过");

    ringbuf_reset(&rb);
    TF_CHECK_INT(ringbuf_write(&rb, msg, 6U), 6);
    /* 只剩 2 字节空间：只写入一部分 */
    TF_CHECK_INT(ringbuf_write(&rb, "xyz", 3U), 2);
    TF_CHECK_INT(ringbuf_free(&rb), 0);

    TF_CHECK_INT(ringbuf_skip(&rb, 3U), 3);
    TF_CHECK_INT(ringbuf_available(&rb), 5);

    ringbuf_reset(&rb);
    TF_CHECK_INT(ringbuf_available(&rb), 0);
    TF_CHECK_INT(ringbuf_write(&rb, "ab", 2U), 2);

    tf_end();

    tf_begin("ringbuf/NULL 安全");

    TF_CHECK_INT(ringbuf_available(NULL), 0);
    TF_CHECK_INT(ringbuf_free(NULL), 0);
    TF_CHECK_INT(ringbuf_write(NULL, msg, 1U), 0);
    TF_CHECK_INT(ringbuf_read(NULL, out, 1U), 0);
    TF_CHECK_INT(ringbuf_peek(NULL, out, 1U), 0);
    TF_CHECK_INT(ringbuf_skip(NULL, 1U), 0);
    ringbuf_reset(NULL);
    TF_CHECK(1);

    tf_end();
}

/* ============================== fsm ============================== */

static int s_entry_count;
static int s_exit_count;
static int s_action_count;
static uint32_t s_last_event;
static bool s_guard_allow;

static void fsm_entry_cb(fsm_t *fsm, void *ctx)
{
    (void)fsm;
    (void)ctx;
    s_entry_count++;
}

static void fsm_exit_cb(fsm_t *fsm, void *ctx)
{
    (void)fsm;
    (void)ctx;
    s_exit_count++;
}

static void fsm_action_cb(fsm_t *fsm, void *ctx, uint32_t event, void *data)
{
    (void)fsm;
    (void)ctx;
    (void)data;
    s_action_count++;
    s_last_event = event;
}

static bool fsm_guard_cb(fsm_t *fsm, void *ctx, uint32_t event, void *data)
{
    (void)fsm;
    (void)ctx;
    (void)event;
    (void)data;
    return s_guard_allow;
}

void ut_fsm(void)
{
    static const fsm_state_t states[] = {
        { 1U, fsm_entry_cb, fsm_action_cb, fsm_exit_cb, NULL },
        { 2U, fsm_entry_cb, fsm_action_cb, fsm_exit_cb, NULL },
        { 3U, fsm_entry_cb, fsm_action_cb, fsm_exit_cb, NULL },
    };
    static const fsm_transition_t trans[] = {
        { 1U, 10U, 2U, NULL },            /* 1 --10--> 2 */
        { 2U, 20U, 3U, fsm_guard_cb },    /* 2 --20--> 3（带守卫） */
        { 3U, 30U, 1U, NULL },            /* 3 --30--> 1 */
    };
    fsm_t fsm;

    tf_begin("fsm/初始化与启动");

    TF_CHECK_INT(fsm_init(NULL, states, 3U, trans, 3U, NULL), -1);
    TF_CHECK_INT(fsm_init(&fsm, NULL, 3U, trans, 3U, NULL), -1);
    TF_CHECK_INT(fsm_init(&fsm, states, 0U, trans, 3U, NULL), -1);
    TF_CHECK_INT(fsm_init(&fsm, states, 3U, trans, 3U, NULL), 0);

    TF_CHECK(!fsm_is_running(&fsm));
    TF_CHECK_INT(fsm_get_state(&fsm), 0);
    TF_CHECK_INT(fsm_dispatch(&fsm, 10U, NULL), -1);   /* 未启动 */

    s_entry_count = 0;
    TF_CHECK_INT(fsm_start(&fsm, 1U), 0);
    TF_CHECK_INT(s_entry_count, 1);
    TF_CHECK(fsm_is_running(&fsm));
    TF_CHECK(fsm_is_state(&fsm, 1U));
    TF_CHECK_INT(fsm_start(&fsm, 99U), -1);            /* 状态不存在 */

    tf_end();

    tf_begin("fsm/事件派发与转换");

    s_action_count = 0;
    s_exit_count = 0;
    s_entry_count = 0;

    TF_CHECK_INT(fsm_dispatch(&fsm, 10U, NULL), 0);
    TF_CHECK_INT(fsm_get_state(&fsm), 2);
    TF_CHECK_INT(s_action_count, 1);
    TF_CHECK_INT(s_last_event, 10);
    TF_CHECK_INT(s_exit_count, 1);
    TF_CHECK_INT(s_entry_count, 1);

    /* 守卫拒绝：状态不变，但 action 已执行 */
    s_guard_allow = false;
    TF_CHECK_INT(fsm_dispatch(&fsm, 20U, NULL), 0);
    TF_CHECK_INT(fsm_get_state(&fsm), 2);

    /* 守卫放行 */
    s_guard_allow = true;
    TF_CHECK_INT(fsm_dispatch(&fsm, 20U, NULL), 0);
    TF_CHECK_INT(fsm_get_state(&fsm), 3);

    /* 无匹配转换：状态不变 */
    TF_CHECK_INT(fsm_dispatch(&fsm, 99U, NULL), 0);
    TF_CHECK_INT(fsm_get_state(&fsm), 3);

    tf_end();

    tf_begin("fsm/暂停、强制切换与停止");

    TF_CHECK_INT(fsm_pause(&fsm), 0);
    TF_CHECK(fsm.paused);
    TF_CHECK_INT(fsm_dispatch(&fsm, 30U, NULL), -1);
    TF_CHECK_INT(fsm_resume(&fsm), 0);
    TF_CHECK_INT(fsm_dispatch(&fsm, 30U, NULL), 0);
    TF_CHECK_INT(fsm_get_state(&fsm), 1);

    TF_CHECK_INT(fsm_transition(&fsm, 2U), 0);
    TF_CHECK_INT(fsm_get_state(&fsm), 2);
    TF_CHECK_INT(fsm_transition(&fsm, 99U), -1);

    TF_CHECK_INT(fsm_stop(&fsm), 0);
    TF_CHECK(!fsm_is_running(&fsm));
    TF_CHECK_INT(fsm_stop(NULL), -1);
    TF_CHECK_INT(fsm_pause(NULL), -1);
    TF_CHECK_INT(fsm_resume(NULL), -1);
    TF_CHECK_INT(fsm_transition(NULL, 1U), -1);

    tf_end();
}
