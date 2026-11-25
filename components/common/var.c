/*
 * var.c - 通用值容器的读写实现
 *
 * 所有接口都做 NULL 与类型检查：类型不匹配时读取失败（返回 false/NULL），
 * 不会做隐式数值转换，避免调用方拿到意外值。
 */

#include "var.h"

#include <stddef.h>

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

void var_set_int(var_t *v, int32_t value)
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

bool var_get_int(const var_t *v, int32_t *out)
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
