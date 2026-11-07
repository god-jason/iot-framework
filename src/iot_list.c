#include "iot_list.h"

void iot_list_init(iot_list_t *list)
{
    if (list == NULL) {
        return;
    }
    list->head.prev = &list->head;
    list->head.next = &list->head;
    list->size = 0;   /* 保留字段，当前不使用；大小见 iot_list_size() */
}

void iot_list_node_init(iot_list_node_t *node)
{
    if (node == NULL) {
        return;
    }
    node->prev = node;
    node->next = node;
}

static void list_link(iot_list_node_t *prev, iot_list_node_t *node,
                      iot_list_node_t *next)
{
    node->prev = prev;
    node->next = next;
    prev->next = node;
    next->prev = node;
}

static bool list_linked(const iot_list_node_t *node)
{
    return node->prev != node && node->next != node;
}

void iot_list_insert_before(iot_list_node_t *pos, iot_list_node_t *node)
{
    if (pos == NULL || node == NULL) {
        return;
    }
    list_link(pos->prev, node, pos);
}

void iot_list_insert_after(iot_list_node_t *pos, iot_list_node_t *node)
{
    if (pos == NULL || node == NULL) {
        return;
    }
    list_link(pos, node, pos->next);
}

void iot_list_add(iot_list_t *list, iot_list_node_t *node)
{
    if (list == NULL || node == NULL) {
        return;
    }
    iot_list_insert_after(&list->head, node);
}

void iot_list_add_tail(iot_list_t *list, iot_list_node_t *node)
{
    if (list == NULL || node == NULL) {
        return;
    }
    iot_list_insert_before(&list->head, node);
}

void iot_list_remove(iot_list_node_t *node)
{
    if (node == NULL || !list_linked(node)) {
        return;
    }
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node;
    node->next = node;
}

void iot_list_move(iot_list_t *list, iot_list_node_t *node)
{
    if (list == NULL || node == NULL) {
        return;
    }
    if (list_linked(node)) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->prev = node;
        node->next = node;
    }
    iot_list_insert_after(&list->head, node);
}

bool iot_list_empty(const iot_list_t *list)
{
    if (list == NULL) {
        return true;
    }
    return list->head.next == &list->head;
}

uint32_t iot_list_size(const iot_list_t *list)
{
    const iot_list_node_t *pos;
    uint32_t n = 0;

    if (list == NULL) {
        return 0;
    }
    for (pos = list->head.next; pos != &list->head; pos = pos->next) {
        n++;
    }
    return n;
}

iot_list_node_t *iot_list_first(const iot_list_t *list)
{
    if (list == NULL || iot_list_empty(list)) {
        return NULL;
    }
    return list->head.next;
}

iot_list_node_t *iot_list_next(const iot_list_node_t *node)
{
    if (node == NULL || node->next == NULL) {
        return NULL;
    }
    return node->next;
}