#ifndef MAP_H
#define MAP_H

/*
 * map.h - 字符串键映射表（跳表实现，节点动态分配）
 *
 * 设计：
 *   - 底层为跳表（skip list），查找/插入/删除平均 O(log n)；
 *   - 节点与键副本通过 ros_mem 分配，使用 map_deinit() 统一释放；
 *   - key 会复制一份，调用者无需保证其生命周期；
 *   - value 为 void*，由调用者管理；
 *   - 层数随机生成，最大 MAP_MAX_LEVEL 层。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAP_MAX_LEVEL
#define MAP_MAX_LEVEL 16U
#endif

/* 单个键值对（map_at 返回该类型） */
typedef struct {
    const char *key;    /* 键字符串（由 map 持有副本） */
    void       *value;  /* 值指针，可为 NULL */
} map_item_t;

typedef struct map_node map_node_t;

/* 映射表：内部字段由 map_init 初始化，调用者不要修改 */
typedef struct {
    map_node_t *head;   /* 跳表哨兵头节点（内部） */
    uint32_t    count;  /* 当前条目数 */
    uint32_t    seed;   /* 随机层数种子（内部） */
} map_t;

/*
 * 初始化映射表（分配哨兵头节点）。
 * 返回 0 成功；m 为 NULL 或内存不足返回 -1。
 * 使用完毕后必须调用 map_deinit() 释放所有节点。
 */
int map_init(map_t *m);

/* 释放全部节点与哨兵头节点，调用后可再次 map_init */
void map_deinit(map_t *m);

/* 清空所有条目（保留哨兵头节点，可继续使用） */
void map_clear(map_t *m);

/* 当前条目数；m 为 NULL 返回 0 */
static inline uint32_t map_size(const map_t *m)
{
    return (m != NULL) ? m->count : 0U;
}

/* 是否包含 key（大小写敏感） */
bool map_contains(const map_t *m, const char *key);

/*
 * 插入或更新。
 * key 已存在时更新 value；否则复制 key 并插入新节点。
 * 参数非法或内存不足返回 false。
 */
bool map_put(map_t *m, const char *key, void *value);

/*
 * 查询：找到时返回 true，并在 out 非 NULL 时写入 value。
 * 该接口可区分“键不存在”和“值为 NULL”。
 */
bool map_get(const map_t *m, const char *key, void **out);

/* 查询并直接返回 value；未找到或值为 NULL 均返回 NULL */
void *map_get_ptr(const map_t *m, const char *key);

/* 按键删除（释放键副本与节点）；未找到返回 false */
bool map_remove(map_t *m, const char *key);

/* 按下标访问（0 .. map_size()-1，O(n)）；越界返回 NULL */
const map_item_t *map_at(const map_t *m, uint32_t index);

/*
 * 迭代器：按键升序遍历（跳表第 0 层 next 链）。
 * node 为内部游标，调用者不要修改；遍历期间不要增删条目。
 */
typedef struct {
    const map_node_t *node;
} map_iter_t;

/*
 * 迭代开始：把游标定位到最小键。
 * m 为 NULL（或已释放）时迭代器为空，后续 map_iter_next 直接返回 false。
 */
void map_iter_begin(const map_t *m, map_iter_t *it);

/*
 * 取下一个条目并前移游标。
 * 条目 key 为 map 持有的副本，value 可能为 NULL；结束或参数非法返回 false。
 * 复杂度均摊 O(1)（单次调用）。
 */
bool map_iter_next(map_iter_t *it, map_item_t *out);

/*
 * 按键升序遍历并调用 fn(item, ctx)，O(n)。
 * fn 返回 false 时提前停止并返回 false；完整遍历结束返回 true。
 * m 为 NULL、未初始化或 fn 为 NULL 时返回 false。
 * 注意：遍历期间不允许增删条目（会使迭代失效）。
 */
bool map_foreach(const map_t *m,
                 bool (*fn)(const map_item_t *item, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* MAP_H */
