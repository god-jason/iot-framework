/* src/iot_list.c per-file 单元测试。 */
#include "test_framework.h"
#include "iot_list.h"

typedef struct {
    int value;
    iot_list_node_t node;
} list_item_t;

static void item_init(list_item_t *it, int value)
{
    it->value = value;
    iot_list_node_init(&it->node);
}

static int first_value(const iot_list_t *list)
{
    iot_list_node_t *n = iot_list_first(list);
    if (n == NULL) {
        return -1;
    }
    return IOT_CONTAINER_OF(n, list_item_t, node)->value;
}

void ut_iot_list(void)
{
    iot_list_t list;
    iot_list_t other;
    list_item_t a, b, c, d;
    iot_list_node_t *n;
    int values[4];
    uint32_t count;

    tf_begin("list 初始化与空表");
    iot_list_init(&list);
    TF_CHECK(iot_list_empty(&list));
    TF_CHECK_INT(iot_list_size(&list), 0);
    TF_CHECK(iot_list_first(&list) == NULL);
    TF_CHECK(iot_list_empty(NULL));
    TF_CHECK_INT(iot_list_size(NULL), 0);
    TF_CHECK(iot_list_first(NULL) == NULL);
    TF_CHECK(iot_list_next(NULL) == NULL);
    iot_list_init(NULL);
    iot_list_node_init(NULL);
    tf_end();

    tf_begin("list 头插/尾插顺序");
    item_init(&a, 1);
    item_init(&b, 2);
    item_init(&c, 3);
    iot_list_init(&list);
    iot_list_add(&list, &a.node);       /* A */
    iot_list_add(&list, &b.node);       /* B A */
    iot_list_add_tail(&list, &c.node);  /* B A C */
    TF_CHECK(!iot_list_empty(&list));
    TF_CHECK_INT(iot_list_size(&list), 3);
    TF_CHECK_INT(first_value(&list), 2);

    count = 0;
    for (n = iot_list_first(&list); n != &list.head; n = iot_list_next(n)) {
        if (count < 4) {
            values[count] = IOT_CONTAINER_OF(n, list_item_t, node)->value;
        }
        count++;
    }
    TF_CHECK_INT(count, 3);
    TF_CHECK_INT(values[0], 2);
    TF_CHECK_INT(values[1], 1);
    TF_CHECK_INT(values[2], 3);

    /* 非法参数不应改变链表 */
    iot_list_add(NULL, &a.node);
    iot_list_add(&list, NULL);
    iot_list_add_tail(NULL, &a.node);
    iot_list_add_tail(&list, NULL);
    TF_CHECK_INT(iot_list_size(&list), 3);
    tf_end();

    tf_begin("list 插入定位与 move");
    item_init(&d, 4);
    iot_list_insert_after(&a.node, &d.node);   /* B A D C */
    TF_CHECK_INT(iot_list_size(&list), 4);
    count = 0;
    for (n = iot_list_first(&list); n != &list.head; n = iot_list_next(n)) {
        if (count < 4) {
            values[count] = IOT_CONTAINER_OF(n, list_item_t, node)->value;
        }
        count++;
    }
    TF_CHECK_INT(values[0], 2);
    TF_CHECK_INT(values[1], 1);
    TF_CHECK_INT(values[2], 4);
    TF_CHECK_INT(values[3], 3);

    iot_list_init(&other);
    iot_list_move(&other, &d.node);     /* 从 list 移到 other */
    TF_CHECK_INT(iot_list_size(&list), 3);
    TF_CHECK_INT(iot_list_size(&other), 1);
    TF_CHECK_INT(first_value(&other), 4);

    /* 已脱离链表的节点再次 move 仍可挂入 */
    iot_list_remove(&d.node);
    TF_CHECK(iot_list_empty(&other));
    iot_list_move(&other, &d.node);
    TF_CHECK_INT(iot_list_size(&other), 1);

    /* 先从 other 摘除，避免同一节点同时挂在两个链表上 */
    iot_list_remove(&d.node);
    TF_CHECK(iot_list_empty(&other));
    iot_list_insert_before(&list.head, &d.node); /* 尾插到 list */
    TF_CHECK_INT(iot_list_size(&list), 4);
    TF_CHECK_INT(iot_list_size(&other), 0);

    iot_list_insert_before(NULL, &a.node);
    iot_list_insert_after(NULL, &a.node);
    TF_CHECK_INT(iot_list_size(&list), 4);
    tf_end();

    tf_begin("list 删除与遍历中删除");
    iot_list_remove(&b.node);
    TF_CHECK_INT(iot_list_size(&list), 3);
    TF_CHECK_INT(first_value(&list), 1);

    /* 遍历时先取 next 再删除 */
    n = iot_list_first(&list);
    while (n != &list.head) {
        iot_list_node_t *next = iot_list_next(n);
        iot_list_remove(n);
        n = next;
    }
    TF_CHECK(iot_list_empty(&list));
    TF_CHECK_INT(iot_list_size(&list), 0);
    TF_CHECK(iot_list_first(&list) == NULL);
    tf_end();

    tf_begin("list 重复删除与未挂链节点");
    item_init(&a, 11);
    item_init(&b, 12);
    iot_list_init(&list);
    iot_list_add_tail(&list, &a.node);
    iot_list_remove(&a.node);
    TF_CHECK_INT(iot_list_size(&list), 0);
    iot_list_remove(&a.node);       /* 重复删除应为无操作 */
    TF_CHECK_INT(iot_list_size(&list), 0);
    TF_CHECK(iot_list_empty(&list));

    /* 未初始化的节点也可安全删除 */
    iot_list_remove(&b.node);
    TF_CHECK_INT(iot_list_size(&list), 0);

    /* 销毁语义：重新 init 后从头开始 */
    iot_list_add_tail(&list, &b.node);
    TF_CHECK_INT(iot_list_size(&list), 1);
    iot_list_init(&list);
    TF_CHECK(iot_list_empty(&list));
    TF_CHECK_INT(iot_list_size(&list), 0);
    tf_end();
}
