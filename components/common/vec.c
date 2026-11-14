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
