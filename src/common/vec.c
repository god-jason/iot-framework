/*
 * vec.c - 变长数组实现（ros_mem 动态扩容）
 */

#include "vec.h"
#include "ros.h"

#include <string.h>

/* 元素地址 */
static uint8_t *vec_addr(const vec_t *v, uint32_t index)
{
    return v->data + (size_t)index * v->elem_size;
}

static bool vec_valid(const vec_t *v)
{
    return v != NULL && v->elem_size != 0U;
}

int vec_reserve(vec_t *v, uint32_t cap)
{
    uint32_t newcap;
    uint8_t *data;

    if (!vec_valid(v)) {
        return -1;
    }
    if (cap <= v->cap) {
        return 0;
    }

    newcap = (v->cap != 0U) ? v->cap : 4U;
    while (newcap < cap) {
        if (newcap > UINT32_MAX / 2U) {
            newcap = cap;
            break;
        }
        newcap *= 2U;
    }
    /* 防止 newcap * elem_size 在 32 位下溢出 */
    if (newcap > UINT32_MAX / v->elem_size) {
        return -1;
    }

    data = (uint8_t *)ros_mem_realloc(v->data,
                                      (size_t)newcap * v->elem_size);
    if (data == NULL) {
        return -1;
    }
    v->data = data;
    v->cap = newcap;
    return 0;
}

int vec_init(vec_t *v, uint32_t elem_size, uint32_t cap)
{
    if (v == NULL || elem_size == 0U) {
        return -1;
    }
    v->data = NULL;
    v->elem_size = elem_size;
    v->len = 0U;
    v->cap = 0U;
    return vec_reserve(v, cap);
}

void vec_deinit(vec_t *v)
{
    if (v == NULL) {
        return;
    }
    ros_mem_free(v->data);
    v->data = NULL;
    v->len = 0U;
    v->cap = 0U;
    v->elem_size = 0U;
}

int vec_push(vec_t *v, const void *elem)
{
    if (!vec_valid(v) || elem == NULL) {
        return -1;
    }
    if (v->len >= v->cap && vec_reserve(v, v->len + 1U) != 0) {
        return -1;
    }
    memcpy(vec_addr(v, v->len), elem, v->elem_size);
    v->len++;
    return 0;
}

int vec_pop(vec_t *v, void *out)
{
    if (!vec_valid(v) || v->len == 0U) {
        return -1;
    }
    v->len--;
    if (out != NULL) {
        memcpy(out, vec_addr(v, v->len), v->elem_size);
    }
    return 0;
}

int vec_insert(vec_t *v, uint32_t index, const void *elem)
{
    if (!vec_valid(v) || elem == NULL || index > v->len) {
        return -1;
    }
    if (v->len >= v->cap && vec_reserve(v, v->len + 1U) != 0) {
        return -1;
    }
    if (index < v->len) {
        memmove(vec_addr(v, index + 1U), vec_addr(v, index),
                (size_t)(v->len - index) * v->elem_size);
    }
    memcpy(vec_addr(v, index), elem, v->elem_size);
    v->len++;
    return 0;
}

int vec_remove(vec_t *v, uint32_t index, void *out)
{
    if (!vec_valid(v) || index >= v->len) {
        return -1;
    }
    if (out != NULL) {
        memcpy(out, vec_addr(v, index), v->elem_size);
    }
    if (index + 1U < v->len) {
        memmove(vec_addr(v, index), vec_addr(v, index + 1U),
                (size_t)(v->len - index - 1U) * v->elem_size);
    }
    v->len--;
    return 0;
}

int vec_resize(vec_t *v, uint32_t len)
{
    uint32_t old;

    if (!vec_valid(v)) {
        return -1;
    }
    if (len > v->cap && vec_reserve(v, len) != 0) {
        return -1;
    }
    old = v->len;
    if (len > old) {
        memset(vec_addr(v, old), 0,
               (size_t)(len - old) * v->elem_size);
    }
    v->len = len;
    return 0;
}

int32_t vec_find_from(const vec_t *v, uint32_t start, const void *elem)
{
    uint32_t i;

    if (!vec_valid(v) || elem == NULL || start >= v->len) {
        return -1;
    }
    for (i = start; i < v->len; i++) {
        if (memcmp(vec_addr(v, i), elem, v->elem_size) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t vec_find(const vec_t *v, const void *elem)
{
    return vec_find_from(v, 0U, elem);
}

int32_t vec_find_if(const vec_t *v,
                    bool (*pred)(const void *elem, void *ctx), void *ctx)
{
    uint32_t i;

    if (!vec_valid(v) || pred == NULL) {
        return -1;
    }
    for (i = 0U; i < v->len; i++) {
        if (pred(vec_addr(v, i), ctx)) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* 逐字节交换，避免按 elem_size 申请临时缓冲区 */
static void vec_swap_bytes(uint8_t *a, uint8_t *b, uint32_t n)
{
    uint32_t i;
    uint8_t t;

    for (i = 0U; i < n; i++) {
        t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

/* 下沉调整：root 为堆根下标，end 为堆元素个数（不含） */
static void vec_sift_down(vec_t *v, uint32_t root, uint32_t end,
                          int (*cmp)(const void *a, const void *b))
{
    uint32_t child;

    while ((child = root * 2U + 1U) < end) {
        /* 取较大子节点 */
        if (child + 1U < end &&
            cmp(vec_addr(v, child), vec_addr(v, child + 1U)) < 0) {
            child++;
        }
        if (cmp(vec_addr(v, root), vec_addr(v, child)) >= 0) {
            break;
        }
        vec_swap_bytes(vec_addr(v, root), vec_addr(v, child), v->elem_size);
        root = child;
    }
}

void vec_sort(vec_t *v, int (*cmp)(const void *a, const void *b))
{
    uint32_t i;

    if (!vec_valid(v) || cmp == NULL || v->len < 2U) {
        return;
    }
    /* 建大顶堆 */
    for (i = v->len / 2U; i > 0U; i--) {
        vec_sift_down(v, i - 1U, v->len, cmp);
    }
    /* 依次把堆顶换到末尾并缩小堆范围 */
    for (i = v->len - 1U; i > 0U; i--) {
        vec_swap_bytes(vec_addr(v, 0U), vec_addr(v, i), v->elem_size);
        vec_sift_down(v, 0U, i, cmp);
    }
}

void vec_reverse(vec_t *v)
{
    uint32_t i;
    uint32_t j;

    if (!vec_valid(v) || v->len < 2U) {
        return;
    }
    for (i = 0U, j = v->len - 1U; i < j; i++, j--) {
        vec_swap_bytes(vec_addr(v, i), vec_addr(v, j), v->elem_size);
    }
}
