#ifndef IOT_LIST_H
#define IOT_LIST_H

/*
 * 侵入式双向链表（参考 Linux Kernel list_head）。
 * 链表节点嵌入业务结构体，避免额外内存分配。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iot_list_node {
    struct iot_list_node *prev;
    struct iot_list_node *next;
} iot_list_node_t;

typedef struct {
    iot_list_node_t head;
    uint32_t size;
} iot_list_t;

/* 由成员指针反推容器对象指针 */
#define IOT_CONTAINER_OF(ptr, type, member) \
    ((type *)((uint8_t *)(ptr) - offsetof(type, member)))

void iot_list_init(iot_list_t *list);
void iot_list_node_init(iot_list_node_t *node);

void iot_list_add(iot_list_t *list, iot_list_node_t *node);
void iot_list_add_tail(iot_list_t *list, iot_list_node_t *node);
void iot_list_insert_before(iot_list_node_t *pos, iot_list_node_t *node);
void iot_list_insert_after(iot_list_node_t *pos, iot_list_node_t *node);

void iot_list_remove(iot_list_node_t *node);
void iot_list_move(iot_list_t *list, iot_list_node_t *node);

bool iot_list_empty(const iot_list_t *list);
uint32_t iot_list_size(const iot_list_t *list);

/* 首元素 / 后继节点（便于遍历，未判定尾节点） */
iot_list_node_t *iot_list_first(const iot_list_t *list);
iot_list_node_t *iot_list_next(const iot_list_node_t *node);

#ifdef __cplusplus
}
#endif

#endif /* IOT_LIST_H */