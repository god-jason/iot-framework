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

/*
 * 条件查找首个满足 pred 的节点。
 * 参数：list - 目标链表；pred - 判定函数；ctx - 透传上下文
 * 返回：命中的节点；未找到或参数非法返回 NULL
 * 复杂度：O(n)
 */
list_node_t *list_find(const list_t *list,
                       bool (*pred)(const list_node_t *node, void *ctx),
                       void *ctx)
{
    list_node_t *pos;

    if (list == NULL || pred == NULL) {
        return NULL;
    }
    for (pos = list->head.next; pos != &list->head; pos = pos->next) {
        if (pred(pos, ctx)) {
            return pos;
        }
    }
    return NULL;
}

/*
 * 按 sort_key 精确匹配查找（键值相同返回首个，O(n)）。
 * 参数：list - 目标链表；sort_key - 待查键值
 * 返回：命中的节点；未找到或 list 为 NULL 返回 NULL
 */
list_node_t *list_find_key(const list_t *list, uint32_t sort_key)
{
    list_node_t *pos;

    if (list == NULL) {
        return NULL;
    }
    for (pos = list->head.next; pos != &list->head; pos = pos->next) {
        if (pos->sort_key == sort_key) {
            return pos;
        }
    }
    return NULL;
}

/*
 * 归并两段相邻单链（各 na / nb 个节点，按 next 连接）。
 * 返回合并后的首节点，并通过 tail_out 回传尾节点；调用方保证 na+nb > 0。
 * 比较结果相等时优先取左段，保证排序稳定。
 */
static list_node_t *list_merge_runs(list_node_t *a, uint32_t na,
                                    list_node_t *b, uint32_t nb,
                                    int (*cmp)(const list_node_t *,
                                               const list_node_t *),
                                    list_node_t **tail_out)
{
    list_node_t dummy;
    list_node_t *tail = &dummy;
    uint32_t ia = 0U;
    uint32_t ib = 0U;

    dummy.next = NULL;
    while (ia < na && ib < nb) {
        if (cmp(a, b) <= 0) {
            tail->next = a;
            a = a->next;
            ia++;
        } else {
            tail->next = b;
            b = b->next;
            ib++;
        }
        tail = tail->next;
    }
    while (ia < na) {
        tail->next = a;
        a = a->next;
        ia++;
        tail = tail->next;
    }
    while (ib < nb) {
        tail->next = b;
        b = b->next;
        ib++;
        tail = tail->next;
    }
    tail->next = NULL;
    *tail_out = tail;
    return dummy.next;
}

/*
 * 稳定归并排序（自底向上），不分配内存。
 *
 * 方案：
 *   1) 遍历统计节点数并找到尾节点，把尾节点 next 置 NULL，拆成单链；
 *      同时把哨兵头恢复为自指空环；
 *   2) 以 width 为段长逐轮把相邻两段归并，每轮段长翻倍，共 O(log n) 轮；
 *   3) 按排序后的单链重新填好每个节点的 prev/next，并把首尾接回哨兵头，
 *      恢复双向循环结构（next 正向、prev 反向均可遍历）。
 *
 * 复杂度：时间 O(n log n)，空间 O(1)（仅若干指针与栈上哑节点）。
 */
void list_sort(list_t *list,
               int (*cmp)(const list_node_t *a, const list_node_t *b))
{
    list_node_t *cur;
    list_node_t *last;
    list_node_t *single;
    list_node_t *prev;
    uint32_t n = 0U;
    uint32_t width;

    if (list == NULL || cmp == NULL || list_empty(list)) {
        return;
    }

    /* 1) 统计并断环 */
    last = &list->head;
    for (cur = list->head.next; cur != &list->head; cur = cur->next) {
        last = cur;
        n++;
    }
    last->next = NULL;
    single = list->head.next;
    list->head.next = &list->head;
    list->head.prev = &list->head;

    /* 2) 自底向上归并 */
    for (width = 1U; width < n; width *= 2U) {
        list_node_t *rest = single;
        list_node_t *new_head = NULL;
        list_node_t *new_tail = NULL;

        while (rest != NULL) {
            list_node_t *left = rest;
            list_node_t *right;
            list_node_t *merged;
            list_node_t *run_tail;
            uint32_t lc = 0U;
            uint32_t rc = 0U;

            while (rest != NULL && lc < width) {
                rest = rest->next;
                lc++;
            }
            right = rest;
            while (rest != NULL && rc < width) {
                rest = rest->next;
                rc++;
            }
            merged = list_merge_runs(left, lc, right, rc, cmp, &run_tail);
            if (new_tail == NULL) {
                new_head = merged;
            } else {
                new_tail->next = merged;
            }
            new_tail = run_tail;
        }
        single = new_head;
        if (width > n / 2U) {
            break;      /* 本轮已覆盖全部元素，避免 width 翻倍溢出 */
        }
    }

    /* 3) 重建双向循环结构 */
    prev = &list->head;
    for (cur = single; cur != NULL; cur = cur->next) {
        prev->next = cur;
        cur->prev = prev;
        prev = cur;
    }
    prev->next = &list->head;
    list->head.prev = prev;
}

/*
 * 顺序遍历并调用 fn(node, ctx)。
 * 参数：list - 目标链表；fn - 回调；ctx - 透传上下文
 * 返回：无；fn 为 NULL 或空链表时直接返回
 * 注意：遍历期间不要增删节点，否则可能提前结束或重复访问。
 */
void list_foreach(const list_t *list,
                  void (*fn)(list_node_t *node, void *ctx), void *ctx)
{
    list_node_t *pos;

    if (list == NULL || fn == NULL) {
        return;
    }
    for (pos = list->head.next; pos != &list->head; pos = pos->next) {
        fn(pos, ctx);
    }
}
