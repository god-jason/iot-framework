/*
 * list.c - 侵入式双向循环链表
 *
 * 模块职责：
 *   提供类似 Linux kernel list_head 的双向循环链表原语。链表节点（prev/next）
 *   直接内嵌在业务结构体中，插入/删除不需要额外分配内存。
 *
 * 数据流与依赖：
 *   仅依赖 标准头文件（bool/uint32_t 等）。链表以“哨兵头节点”组织：
 *   list->head 不承载数据，head.next 为首元素、head.prev 为尾元素；
 *   空链表时 head.next == head.prev == &head。
 *
 * 静态数据与边界约束：
 *   - 无全局状态，所有状态都在调用者提供的 list_t / 节点中。
 *   - 不做重复插入检测：已挂入链表的节点再次插入会导致链表损坏，
 *     需先用 list_remove() 摘除。
 *   - 节点脱离链表后 prev/next 均指向自身，可用于判断“是否在链上”。
 *   - list_t.size 字段保留但从不更新，长度以 list_size() 遍历为准。
 */

#include "list.h"

/*
 * 初始化链表为空的循环结构（头节点自指）。
 * 参数：list - 待初始化链表，为 NULL 时直接返回
 * 返回：无
 */
void list_init(list_t *list)
{
    if (list == NULL) {
        return;
    }
    list->head.prev = &list->head;
    list->head.next = &list->head;
    list->size = 0;   /* 保留字段，当前不使用；大小见 list_size() */
}

/*
 * 把单个节点初始化为“未挂接”状态（prev/next 指向自身）。
 * 参数：node - 节点，为 NULL 时直接返回
 * 返回：无
 * 注意：插入前应保证节点处于该状态，否则链表指针可能被覆盖。
 */
void list_node_init(list_node_t *node)
{
    if (node == NULL) {
        return;
    }
    node->prev = node;
    node->next = node;
    node->sort_key = 0U;
}

/*
 * 把 node 链接到 prev 与 next 之间。
 * 参数：prev - 前驱；node - 待插入节点；next - 后继
 * 返回：无
 * 注意：仅调整指针，不做任何合法性校验，供内部使用。
 */
static void list_link(list_node_t *prev, list_node_t *node,
                      list_node_t *next)
{
    node->prev = prev;
    node->next = next;
    prev->next = node;
    next->prev = node;
}

/*
 * 判断节点是否已链接进某个链表。
 * 参数：node - 待判断节点
 * 返回：true 表示 prev/next 不同时指向自身（即在链上）；false 表示游离
 * 注意：该判断以“游离节点自指”为约定，不检查节点是否属于特定链表。
 */
static bool list_linked(const list_node_t *node)
{
    return node->prev != node && node->next != node;
}

/*
 * 把 node 插入到 pos 之前。
 * 参数：pos - 参考节点；node - 待插入节点
 * 返回：无；任一参数为 NULL 时直接返回
 * 注意：要求 node 当前不在任何链表上，否则会破坏原链表。
 */
void list_insert_before(list_node_t *pos, list_node_t *node)
{
    if (pos == NULL || node == NULL) {
        return;
    }
    list_link(pos->prev, node, pos);
}

/*
 * 把 node 插入到 pos 之后。
 * 参数：pos - 参考节点；node - 待插入节点
 * 返回：无；任一参数为 NULL 时直接返回
 * 注意：与 insert_before 相同，node 必须处于游离状态。
 */
void list_insert_after(list_node_t *pos, list_node_t *node)
{
    if (pos == NULL || node == NULL) {
        return;
    }
    list_link(pos, node, pos->next);
}

/*
 * 在链表头部插入节点（head.next 位置）。
 * 参数：list - 目标链表；node - 待插入节点（必须游离）
 * 返回：无；任一参数为 NULL 时直接返回
 */
void list_add(list_t *list, list_node_t *node)
{
    if (list == NULL || node == NULL) {
        return;
    }
    list_insert_after(&list->head, node);
}

/*
 * 在链表尾部插入节点（head.prev 位置），保持先进先出顺序。
 * 参数：list - 目标链表；node - 待插入节点（必须游离）
 * 返回：无；任一参数为 NULL 时直接返回
 */
void list_add_tail(list_t *list, list_node_t *node)
{
    if (list == NULL || node == NULL) {
        return;
    }
    list_insert_before(&list->head, node);
}

/*
 * 从链表中摘除节点，并恢复为游离状态。
 * 参数：node - 待摘除节点
 * 返回：无
 * 注意：节点本已游离（或为 NULL）时直接返回，可安全重复调用；
 *       摘除后不清空节点内容，仅重置链接指针。
 */
void list_remove(list_node_t *node)
{
    if (node == NULL || !list_linked(node)) {
        return;
    }
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node;
    node->next = node;
}

/*
 * 把节点移动到链表头部（若节点在别的链表上会先被摘下）。
 * 参数：list - 目标链表；node - 待移动节点
 * 返回：无；任一参数为 NULL 时直接返回
 * 注意：节点已在该链表头部时仍会被摘下再插入，语义上等价但不做优化。
 */
void list_move(list_t *list, list_node_t *node)
{
    if (list == NULL || node == NULL) {
        return;
    }
    if (list_linked(node)) {
        /* 先原地摘除，避免旧链表中残留指针 */
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->prev = node;
        node->next = node;
    }
    list_insert_after(&list->head, node);
}

/*
 * 判断链表是否为空。
 * 参数：list - 目标链表
 * 返回：true 为空；false 非空（list 为 NULL 也视为空，便于判空习惯用法）
 */
bool list_empty(const list_t *list)
{
    if (list == NULL) {
        return true;
    }
    return list->head.next == &list->head;
}

/*
 * 统计链表节点数（O(n) 遍历，不使用保留字段 size）。
 * 参数：list - 目标链表
 * 返回：节点个数；list 为 NULL 时返回 0
 */
uint32_t list_size(const list_t *list)
{
    const list_node_t *pos;
    uint32_t n = 0;

    if (list == NULL) {
        return 0;
    }
    for (pos = list->head.next; pos != &list->head; pos = pos->next) {
        n++;
    }
    return n;
}

/*
 * 取链表首节点。
 * 参数：list - 目标链表
 * 返回：首节点指针；链表为空或 list 为 NULL 时返回 NULL
 */
list_node_t *list_first(const list_t *list)
{
    if (list == NULL || list_empty(list)) {
        return NULL;
    }
    return list->head.next;
}

/*
 * 取节点的后继节点。
 * 参数：node - 当前节点
 * 返回：后继指针；node 为 NULL（或 next 异常为 NULL）时返回 NULL
 * 注意：不判断是否已到尾节点（尾节点的 next 指向哨兵头，非 NULL），
 *       遍历时需由调用者用“不等于 &list->head”作为结束条件。
 */
list_node_t *list_next(const list_node_t *node)
{
    if (node == NULL || node->next == NULL) {
        return NULL;
    }
    return node->next;
}

/*
 * 摘除并返回链表首节点。
 * 参数：list - 目标链表
 * 返回：原首节点；空链表或 list 为 NULL 时返回 NULL
 * 注意：返回的节点已被摘除并恢复游离态。
 */
list_node_t *list_pop_first(list_t *list)
{
    list_node_t *node;

    if (list == NULL || list_empty(list)) {
        return NULL;
    }
    node = list->head.next;
    list_remove(node);
    return node;
}

/*
 * 按节点 sort_key 升序插入。
 * 参数：list - 目标链表；node - 待插入节点（必须游离）
 * 返回：无；任一参数为 NULL 时直接返回
 * 注意：以有符号差值比较，支持 uint32 时间量回绕；
 *       相同键值插入到已有同值节点之后，保持先进先出。
 */
void list_insert_sorted(list_t *list, list_node_t *node)
{
    list_node_t *pos;

    if (list == NULL || node == NULL) {
        return;
    }
    for (pos = list->head.next; pos != &list->head; pos = pos->next) {
        if ((int32_t)(node->sort_key - pos->sort_key) < 0) {
            break;
        }
    }
    list_insert_before(pos, node);
}
