/*
 * list.c - RTOS 内部侵入式双向循环链表
 *
 * 与 components/common/list.h 相互独立；节点带 sort_key，支持按升序插入，
 * 用于延时唤醒链表（键 = wake_tick）与软件定时器链表（键 = deadline）。
 * 仅依赖标准头，无动态内存。
 */

#include "list.h"

void list_init(list_t *list)
{
    if (list == NULL) {
        return;
    }
    list->head.next = &list->head;
    list->head.prev = &list->head;
    list->head.sort_key = 0;
}

void list_node_init(list_node_t *node)
{
    if (node == NULL) {
        return;
    }
    node->next = node;
    node->prev = node;
    node->sort_key = 0;
}

bool list_empty(const list_t *list)
{
    if (list == NULL) {
        return true;
    }
    return list->head.next == &list->head;
}

list_node_t *list_first(const list_t *list)
{
    if (list == NULL || list_empty(list)) {
        return NULL;
    }
    return list->head.next;
}

static void iot_list_unlink(list_node_t *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

static void iot_list_link_before(list_node_t *pos, list_node_t *node)
{
    node->next = pos;
    node->prev = pos->prev;
    pos->prev->next = node;
    pos->prev = node;
}

void list_add(list_t *list, list_node_t *node)
{
    if (list == NULL || node == NULL) {
        return;
    }
    iot_list_link_before(list->head.next, node);
}

void list_add_tail(list_t *list, list_node_t *node)
{
    if (list == NULL || node == NULL) {
        return;
    }
    iot_list_link_before(&list->head, node);
}

list_node_t *list_pop_first(list_t *list)
{
    list_node_t *node;

    if (list == NULL || list_empty(list)) {
        return NULL;
    }
    node = list->head.next;
    iot_list_unlink(node);
    return node;
}

void list_remove(list_node_t *node)
{
    if (node == NULL || node->next == node) {
        return;   /* 游离态或 NULL */
    }
    iot_list_unlink(node);
}

void list_insert_sorted(list_t *list, list_node_t *node)
{
    list_node_t *pos;

    if (list == NULL || node == NULL) {
        return;
    }
    for (pos = list->head.next; pos != &list->head; pos = pos->next) {
        if (pos->sort_key > node->sort_key) {
            break;
        }
    }
    iot_list_link_before(pos, node);
}