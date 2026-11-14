#ifndef VEC_H
#define VEC_H

/*
 * vec.h - 变长数组（动态扩容）
 *
 * 设计：
 *   - 底层为连续内存，按 elem_size 拷贝元素；
 *   - 内存通过 ros_mem_realloc/free 管理，使用完必须 vec_deinit()；
 *   - 容量不足时按 2 倍增长（首次默认 4），均摊 O(1) 追加；
 *   - 元素下标越界/参数非法时返回 -1 或 NULL。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *data;      /* 元素存储区（ros_mem 分配） */
    uint32_t elem_size; /* 单元素字节数 */
    uint32_t len;       /* 当前元素个数 */
    uint32_t cap;       /* 当前容量（元素个数） */
} vec_t;

/*
 * 初始化。
 * cap 为初始容量（可为 0，表示首次插入时再分配）。
 * 返回 0 成功；elem_size 为 0 或内存不足返回 -1。
 */
int vec_init(vec_t *v, uint32_t elem_size, uint32_t cap);

/* 释放存储区；可重复调用 */
void vec_deinit(vec_t *v);

/* 清空元素（保留已分配容量） */
static inline void vec_clear(vec_t *v)
{
    if (v != NULL) {
        v->len = 0U;
    }
}

/* 元素个数 / 容量；v 为 NULL 时返回 0 */
static inline uint32_t vec_size(const vec_t *v)
{
    return (v != NULL) ? v->len : 0U;
}

static inline uint32_t vec_capacity(const vec_t *v)
{
    return (v != NULL) ? v->cap : 0U;
}

/* 是否为空；v 为 NULL 返回 true */
static inline bool vec_empty(const vec_t *v)
{
    return v == NULL || v->len == 0U;
}

/* 取元素指针（可写）；越界或 v 为 NULL 返回 NULL */
static inline void *vec_at(const vec_t *v, uint32_t index)
{
    if (v == NULL || v->elem_size == 0U || index >= v->len) {
        return NULL;
    }
    return (void *)(v->data + (size_t)index * v->elem_size);
}

/* 取原始存储区首地址；空/未分配返回 NULL */
static inline void *vec_data(const vec_t *v)
{
    return (v != NULL) ? (void *)v->data : NULL;
}

/* 预分配容量（不改变 len）；失败返回 -1 */
int vec_reserve(vec_t *v, uint32_t cap);

/* 尾部追加（拷贝 elem_size 字节）；失败返回 -1 */
int vec_push(vec_t *v, const void *elem);

/* 弹出尾部元素到 out（out 可为 NULL）；空返回 -1 */
int vec_pop(vec_t *v, void *out);

/* 在 index 处插入（index <= len）；失败返回 -1 */
int vec_insert(vec_t *v, uint32_t index, const void *elem);

/* 删除 index 处元素，可选出到 out；失败返回 -1 */
int vec_remove(vec_t *v, uint32_t index, void *out);

/*
 * 调整元素个数。
 * 增长时新增区域清零；缩小直接截断。失败返回 -1。
 */
int vec_resize(vec_t *v, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* VEC_H */
