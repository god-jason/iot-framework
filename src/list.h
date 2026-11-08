#ifndef IOT_LIST_H
#define IOT_LIST_H

/*
 * 内核内部侵入式双向循环链表（与 components/common/list.h 相互独立）。
 * 节点内嵌于任务/定时器结构体，通过 IOT_CONTAINER_OF 反推对象；
 * 支持按键值升序插入，用于延时链表与软件定时器链表。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iot_list_node {
    struct iot_list_node *next;
    struct iot_list_node *prev;
    uint32_t sort_key;      /* 用于有序插入的键值（延时/定时器截止时刻） */
} list_node_t;

typedef struct {
    list_node_t head;  /* 哨兵节点，head.next 为首元素 */
} list_t;

/* 与 components/common/list.h 中的同名宏保持一致，避免重复定义告警 */
#ifndef IOT_CONTAINER_OF
#define IOT_CONTAINER_OF(ptr, type, member) \
    ((type *)((uint8_t *)(ptr) - offsetof(type, member)))
#endif

void list_init(list_t *list);
void list_node_init(list_node_t *node);

bool list_empty(const list_t *list);
list_node_t *list_first(const list_t *list);

void list_add(list_t *list, list_node_t *node);
void list_add_tail(list_t *list, list_node_t *node);
list_node_t *list_pop_first(list_t *list);

/* 摘除节点并恢复为游离态；对游离节点/NULL 安全 */
void list_remove(list_node_t *node);

/* 按 sort_key 升序插入（相同键值排在后面，保持 FIFO） */
void list_insert_sorted(list_t *list, list_node_t *node);

#ifdef __cplusplus
}
#endif

#endif /* IOT_LIST_H */