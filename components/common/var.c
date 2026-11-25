/*
 * var.c - 通用值容器的读写实现
 *
 * 所有接口都做 NULL 与类型检查：类型不匹配时读取失败（返回 false/NULL），
 * 不会做隐式数值转换，避免调用方拿到意外值。
 */

#include "var.h"

#include "ros_mem.h"

#include <stddef.h>

/* 递归释放深度上限（防止循环引用导致栈溢出） */
#define VAR_FREE_MAX_DEPTH      8U

void var_init(var_t *v)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_NONE;
    v->value.arr = NULL;    /* 指针为联合体最大成员，可一次性清零 */
}

void var_set_bool(var_t *v, bool value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_BOOL;
    v->value.b = value;
}

void var_set_int(var_t *v, int value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_INT;
    v->value.i = value;
}

void var_set_float(var_t *v, float value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_FLOAT;
    v->value.f = value;
}

void var_set_string(var_t *v, const char *value)
{
    if (v == NULL) {
        return;
    }
    if (value == NULL) {
        var_init(v);
        return;
    }
    v->type = VAR_STRING;
    v->value.s = value;
}

void var_set_array(var_t *v, vec_t *value)
{
    if (v == NULL) {
        return;
    }
    if (value == NULL) {
        var_init(v);
        return;
    }
    v->type = VAR_ARRAY;
    v->value.arr = value;
}

void var_set_object(var_t *v, map_t *value)
{
    if (v == NULL) {
        return;
    }
    if (value == NULL) {
        var_init(v);
        return;
    }
    v->type = VAR_OBJECT;
    v->value.obj = value;
}

bool var_is_none(const var_t *v)
{
    return (v == NULL || v->type == VAR_NONE);
}

bool var_get_bool(const var_t *v, bool *out)
{
    if (v == NULL || out == NULL || v->type != VAR_BOOL) {
        return false;
    }
    *out = v->value.b;
    return true;
}

bool var_get_int(const var_t *v, int *out)
{
    if (v == NULL || out == NULL || v->type != VAR_INT) {
        return false;
    }
    *out = v->value.i;
    return true;
}

bool var_get_float(const var_t *v, float *out)
{
    if (v == NULL || out == NULL || v->type != VAR_FLOAT) {
        return false;
    }
    *out = v->value.f;
    return true;
}

const char *var_get_string(const var_t *v)
{
    if (v == NULL || v->type != VAR_STRING) {
        return NULL;
    }
    return v->value.s;
}

vec_t *var_get_array(const var_t *v)
{
    if (v == NULL || v->type != VAR_ARRAY) {
        return NULL;
    }
    return v->value.arr;
}

map_t *var_get_object(const var_t *v)
{
    if (v == NULL || v->type != VAR_OBJECT) {
        return NULL;
    }
    return v->value.obj;
}

static void var_free_depth(var_t *v, uint32_t depth)
{
    if (v == NULL) {
        return;
    }
    if (depth > VAR_FREE_MAX_DEPTH) {
        var_init(v);                    /* 嵌套过深：放弃释放，防循环引用 */
        return;
    }

    if (v->type == VAR_ARRAY && v->value.arr != NULL) {
        vec_t *arr = v->value.arr;

        if (arr->elem_size == sizeof(var_t)) {
            uint32_t i;

            for (i = 0U; i < arr->len; i++) {
                var_free_depth((var_t *)vec_at(arr, i), depth + 1U);
            }
        }
        vec_deinit(arr);
        ros_mem_free(arr);
    } else if (v->type == VAR_OBJECT && v->value.obj != NULL) {
        map_t *obj = v->value.obj;
        map_iter_t it;
        map_item_t item;

        map_iter_begin(obj, &it);
        while (map_iter_next(&it, &item)) {
            var_t *elem = (var_t *)item.value;

            if (elem != NULL) {
                var_free_depth(elem, depth + 1U);
                ros_mem_free(elem);
            }
        }
        map_deinit(obj);
        ros_mem_free(obj);
    }

    var_init(v);
}

void var_free(var_t *v)
{
    var_free_depth(v, 0U);
}
