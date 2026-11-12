/*
 * list.h - 侵入式双向循环链表接口
 *
 * 节点内嵌于业务结构体，通过 ROS_CONTAINER_OF 由节点指针反推对象指针；
 * 不使用动态内存。空链表以哨兵头节点自指表示。
 */

#ifndef LIST_H
#define LIST_H

/*
 * 侵入式双向链表（参考 Linux Kernel list_head）。
 * 链表节点嵌入业务结构体，避免额外内存分配。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 链表节点：由所在结构体提供存储，游离态时 prev/next 指向自身 */
typedef struct list_node {
    struct list_node *prev;   /* 前驱节点；哨兵头节点时为尾元素 */
    struct list_node *next;   /* 后继节点；哨兵头节点时为首元素 */
    uint32_t sort_key;        /* 有序插入键值（回绕安全的 tick/时刻） */
} list_node_t;

/* 链表头：head 为不承载数据的哨兵节点 */
typedef struct {
    list_node_t head;   /* 哨兵节点，head.next 为首元素 */
    uint32_t size;          /* 保留字段，当前不维护，长度请用 list_size() */
} list_t;

/* 由成员指针反推容器对象指针 */
#define ROS_CONTAINER_OF(ptr, type, member) \
    ((type *)((uint8_t *)(ptr) - offsetof(type, member)))

/* 初始化为空链表（哨兵自指） */
void list_init(list_t *list);
/* 将节点初始化为游离态（prev/next 指向自身） */
void list_node_init(list_node_t *node);

/* 头插：插入到 head.next，即成为新的首元素 */
void list_add(list_t *list, list_node_t *node);
/* 尾插：插入到 head.prev，保持先进先出 */
void list_add_tail(list_t *list, list_node_t *node);
/* 插入到 pos 之前；node 必须处于游离态 */
void list_insert_before(list_node_t *pos, list_node_t *node);
/* 插入到 pos 之后；node 必须处于游离态 */
void list_insert_after(list_node_t *pos, list_node_t *node);

/* 摘除节点并恢复游离态；对游离节点或 NULL 安全 */
void list_remove(list_node_t *node);

/* 摘除并返回首节点；空链表或 list 为 NULL 时返回 NULL */
list_node_t *list_pop_first(list_t *list);

/*
 * 按 node->sort_key 升序插入（键值相同则排在已有同值节点之后，保持 FIFO）。
 * 键值按有符号差值比较，适用于 tick 等会回绕的时间量；
 * 要求所有键值落在同一个 2^31 tick 窗口内。
 */
void list_insert_sorted(list_t *list, list_node_t *node);
/* 把节点移动到链表头部（会先从原链表摘下） */
void list_move(list_t *list, list_node_t *node);

/* 判空；list 为 NULL 时返回 true */
bool list_empty(const list_t *list);
/* 遍历统计节点数；list 为 NULL 时返回 0 */
uint32_t list_size(const list_t *list);

/* 首元素 / 后继节点（便于遍历，未判定尾节点） */
/* 取首节点；空链表返回 NULL */
list_node_t *list_first(const list_t *list);
/* 取后继节点；遍历结束条件需由调用者判断 pos != &list->head */
list_node_t *list_next(const list_node_t *node);

#ifdef __cplusplus
}
#endif

#endif /* LIST_H */
