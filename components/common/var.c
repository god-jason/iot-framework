/*
 * var.c - 通用值容器实现
 * ============================================================================
 *
 * 所有接口都做 NULL 与类型检查，不做隐式转换；
 * 需要跨类型取值时使用 var_to_xxx()，转换失败返回 false。
 *
 * 文件结构：
 *   基础读写 -> 类型转换 -> 释放 -> 深拷贝 -> 内部工具
 *   -> 对象操作 -> 数组操作 -> 遍历
 */

#include "var.h"

#include "ros_mem.h"
#include "strings.h"
#include "var_json.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

/* 释放 / 深拷贝的递归深度上限（防止循环引用） */
#define VAR_MAX_DEPTH       8U

#define VAR_I64_MAX_AS_DOUBLE   (9223372036854775808.0)
#define VAR_U64_MAX_AS_DOUBLE   (18446744073709551616.0)

/* ============================================================================
 * 基础读写
 * ==========================================================================*/

void var_init(var_t *v)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_NONE;
    v->value.u64 = 0;       /* 联合体最大成员，可一次性清零 */
}

void var_set_bool(var_t *v, bool value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_BOOL;
    v->value.b = value;
}

void var_set_int8(var_t *v, int8_t value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_INT8;
    v->value.i8 = value;
}

void var_set_uint8(var_t *v, uint8_t value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_UINT8;
    v->value.u8 = value;
}

void var_set_int16(var_t *v, int16_t value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_INT16;
    v->value.i16 = value;
}

void var_set_uint16(var_t *v, uint16_t value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_UINT16;
    v->value.u16 = value;
}

void var_set_int32(var_t *v, int32_t value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_INT32;
    v->value.i32 = value;
}

void var_set_uint32(var_t *v, uint32_t value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_UINT32;
    v->value.u32 = value;
}

void var_set_int64(var_t *v, int64_t value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_INT64;
    v->value.i64 = value;
}

void var_set_uint64(var_t *v, uint64_t value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_UINT64;
    v->value.u64 = value;
}

void var_set_float32(var_t *v, float value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_FLOAT32;
    v->value.f32 = value;
}

void var_set_float64(var_t *v, double value)
{
    if (v == NULL) {
        return;
    }
    v->type = VAR_FLOAT64;
    v->value.f64 = value;
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

bool var_get_int8(const var_t *v, int8_t *out)
{
    if (v == NULL || out == NULL || v->type != VAR_INT8) {
        return false;
    }
    *out = v->value.i8;
    return true;
}

bool var_get_uint8(const var_t *v, uint8_t *out)
{
    if (v == NULL || out == NULL || v->type != VAR_UINT8) {
        return false;
    }
    *out = v->value.u8;
    return true;
}

bool var_get_int16(const var_t *v, int16_t *out)
{
    if (v == NULL || out == NULL || v->type != VAR_INT16) {
        return false;
    }
    *out = v->value.i16;
    return true;
}

bool var_get_uint16(const var_t *v, uint16_t *out)
{
    if (v == NULL || out == NULL || v->type != VAR_UINT16) {
        return false;
    }
    *out = v->value.u16;
    return true;
}

bool var_get_int32(const var_t *v, int32_t *out)
{
    if (v == NULL || out == NULL || v->type != VAR_INT32) {
        return false;
    }
    *out = v->value.i32;
    return true;
}

bool var_get_uint32(const var_t *v, uint32_t *out)
{
    if (v == NULL || out == NULL || v->type != VAR_UINT32) {
        return false;
    }
    *out = v->value.u32;
    return true;
}

bool var_get_int64(const var_t *v, int64_t *out)
{
    if (v == NULL || out == NULL || v->type != VAR_INT64) {
        return false;
    }
    *out = v->value.i64;
    return true;
}

bool var_get_uint64(const var_t *v, uint64_t *out)
{
    if (v == NULL || out == NULL || v->type != VAR_UINT64) {
        return false;
    }
    *out = v->value.u64;
    return true;
}

bool var_get_float32(const var_t *v, float *out)
{
    if (v == NULL || out == NULL || v->type != VAR_FLOAT32) {
        return false;
    }
    *out = v->value.f32;
    return true;
}

bool var_get_float64(const var_t *v, double *out)
{
    if (v == NULL || out == NULL || v->type != VAR_FLOAT64) {
        return false;
    }
    *out = v->value.f64;
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

/* ============================================================================
 * 类型转换
 * ==========================================================================*/

static bool double_to_i64(double d, int64_t *out)
{
    if (!isfinite(d) || d < -VAR_I64_MAX_AS_DOUBLE ||
        d >= VAR_I64_MAX_AS_DOUBLE) {
        return false;
    }
    *out = (int64_t)d;
    return true;
}

static bool double_to_u64(double d, uint64_t *out)
{
    if (!isfinite(d) || d < 0.0 || d >= VAR_U64_MAX_AS_DOUBLE) {
        return false;
    }
    *out = (uint64_t)d;
    return true;
}

/* 解析十进制有符号 64 位整数：必须整串匹配 */
static bool parse_i64_text(const char *s, int64_t *out)
{
    const char *p = s;
    uint64_t mag = 0U;
    bool neg = false;

    if (s == NULL || out == NULL) {
        return false;
    }
    if (*p == '-' || *p == '+') {
        neg = (*p == '-');
        p++;
    }
    if (*p < '0' || *p > '9') {
        return false;
    }
    while (*p >= '0' && *p <= '9') {
        uint32_t d = (uint32_t)(*p - '0');

        if (mag > (UINT64_MAX - d) / 10U) {
            return false;
        }
        mag = mag * 10U + d;
        p++;
    }
    if (*p != '\0') {
        return false;
    }
    if (neg) {
        if (mag > (uint64_t)INT64_MAX + 1U) {
            return false;
        }
        *out = (mag == (uint64_t)INT64_MAX + 1U) ? INT64_MIN
                                                 : -(int64_t)mag;
    } else {
        if (mag > (uint64_t)INT64_MAX) {
            return false;
        }
        *out = (int64_t)mag;
    }
    return true;
}

/* 解析十进制无符号 64 位整数：必须整串匹配，不接受 '-' */
static bool parse_u64_text(const char *s, uint64_t *out)
{
    const char *p = s;
    uint64_t mag = 0U;

    if (s == NULL || out == NULL) {
        return false;
    }
    if (*p == '+') {
        p++;
    }
    if (*p < '0' || *p > '9') {
        return false;
    }
    while (*p >= '0' && *p <= '9') {
        uint32_t d = (uint32_t)(*p - '0');

        if (mag > (UINT64_MAX - d) / 10U) {
            return false;
        }
        mag = mag * 10U + d;
        p++;
    }
    if (*p != '\0') {
        return false;
    }
    *out = mag;
    return true;
}

/* 读取为有符号 64 位；失败返回 false */
static bool var_read_i64(const var_t *v, int64_t *out)
{
    if (v == NULL || out == NULL) {
        return false;
    }
    switch (v->type) {
    case VAR_BOOL:
        *out = v->value.b ? 1 : 0;
        return true;
    case VAR_INT8:
        *out = v->value.i8;
        return true;
    case VAR_UINT8:
        *out = v->value.u8;
        return true;
    case VAR_INT16:
        *out = v->value.i16;
        return true;
    case VAR_UINT16:
        *out = v->value.u16;
        return true;
    case VAR_INT32:
        *out = v->value.i32;
        return true;
    case VAR_UINT32:
        *out = v->value.u32;
        return true;
    case VAR_INT64:
        *out = v->value.i64;
        return true;
    case VAR_UINT64:
        if (v->value.u64 > (uint64_t)INT64_MAX) {
            return false;
        }
        *out = (int64_t)v->value.u64;
        return true;
    case VAR_FLOAT32:
        return double_to_i64((double)v->value.f32, out);
    case VAR_FLOAT64:
        return double_to_i64(v->value.f64, out);
    case VAR_STRING:
        return parse_i64_text(v->value.s, out);
    default:
        return false;
    }
}

/* 读取为无符号 64 位；失败返回 false */
static bool var_read_u64(const var_t *v, uint64_t *out)
{
    if (v == NULL || out == NULL) {
        return false;
    }
    switch (v->type) {
    case VAR_BOOL:
        *out = v->value.b ? 1U : 0U;
        return true;
    case VAR_INT8:
        if (v->value.i8 < 0) {
            return false;
        }
        *out = (uint64_t)v->value.i8;
        return true;
    case VAR_UINT8:
        *out = v->value.u8;
        return true;
    case VAR_INT16:
        if (v->value.i16 < 0) {
            return false;
        }
        *out = (uint64_t)v->value.i16;
        return true;
    case VAR_UINT16:
        *out = v->value.u16;
        return true;
    case VAR_INT32:
        if (v->value.i32 < 0) {
            return false;
        }
        *out = (uint64_t)v->value.i32;
        return true;
    case VAR_UINT32:
        *out = v->value.u32;
        return true;
    case VAR_INT64:
        if (v->value.i64 < 0) {
            return false;
        }
        *out = (uint64_t)v->value.i64;
        return true;
    case VAR_UINT64:
        *out = v->value.u64;
        return true;
    case VAR_FLOAT32:
        return double_to_u64((double)v->value.f32, out);
    case VAR_FLOAT64:
        return double_to_u64(v->value.f64, out);
    case VAR_STRING:
        return parse_u64_text(v->value.s, out);
    default:
        return false;
    }
}

/* 读取为 double；失败返回 false */
static bool var_read_f64(const var_t *v, double *out)
{
    double d;

    if (v == NULL || out == NULL) {
        return false;
    }
    switch (v->type) {
    case VAR_BOOL:
        d = v->value.b ? 1.0 : 0.0;
        break;
    case VAR_INT8:
        d = (double)v->value.i8;
        break;
    case VAR_UINT8:
        d = (double)v->value.u8;
        break;
    case VAR_INT16:
        d = (double)v->value.i16;
        break;
    case VAR_UINT16:
        d = (double)v->value.u16;
        break;
    case VAR_INT32:
        d = (double)v->value.i32;
        break;
    case VAR_UINT32:
        d = (double)v->value.u32;
        break;
    case VAR_INT64:
        d = (double)v->value.i64;
        break;
    case VAR_UINT64:
        d = (double)v->value.u64;
        break;
    case VAR_FLOAT32:
        d = (double)v->value.f32;
        break;
    case VAR_FLOAT64:
        d = v->value.f64;
        break;
    case VAR_STRING: {
        float f;

        if (!str_parse_float(v->value.s, &f)) {
            return false;
        }
        d = (double)f;
        break;
    }
    default:
        return false;
    }
    if (!isfinite(d)) {
        return false;
    }
    *out = d;
    return true;
}

bool var_to_bool(const var_t *v, bool *out)
{
    if (v == NULL || out == NULL) {
        return false;
    }
    switch (v->type) {
    case VAR_BOOL:
        *out = v->value.b;
        return true;
    case VAR_INT8:
        *out = (v->value.i8 != 0);
        return true;
    case VAR_UINT8:
        *out = (v->value.u8 != 0U);
        return true;
    case VAR_INT16:
        *out = (v->value.i16 != 0);
        return true;
    case VAR_UINT16:
        *out = (v->value.u16 != 0U);
        return true;
    case VAR_INT32:
        *out = (v->value.i32 != 0);
        return true;
    case VAR_UINT32:
        *out = (v->value.u32 != 0U);
        return true;
    case VAR_INT64:
        *out = (v->value.i64 != 0);
        return true;
    case VAR_UINT64:
        *out = (v->value.u64 != 0U);
        return true;
    case VAR_FLOAT32:
        *out = (v->value.f32 != 0.0f);
        return true;
    case VAR_FLOAT64:
        *out = (v->value.f64 != 0.0);
        return true;
    case VAR_STRING:
        if (str_parse_bool(v->value.s, out)) {
            return true;
        }
        *out = (v->value.s != NULL && v->value.s[0] != '\0');
        return true;
    default:
        return false;
    }
}

bool var_to_int8(const var_t *v, int8_t *out)
{
    int64_t x;

    if (out == NULL || !var_read_i64(v, &x) ||
        x < INT8_MIN || x > INT8_MAX) {
        return false;
    }
    *out = (int8_t)x;
    return true;
}

bool var_to_uint8(const var_t *v, uint8_t *out)
{
    uint64_t x;

    if (out == NULL || !var_read_u64(v, &x) || x > UINT8_MAX) {
        return false;
    }
    *out = (uint8_t)x;
    return true;
}

bool var_to_int16(const var_t *v, int16_t *out)
{
    int64_t x;

    if (out == NULL || !var_read_i64(v, &x) ||
        x < INT16_MIN || x > INT16_MAX) {
        return false;
    }
    *out = (int16_t)x;
    return true;
}

bool var_to_uint16(const var_t *v, uint16_t *out)
{
    uint64_t x;

    if (out == NULL || !var_read_u64(v, &x) || x > UINT16_MAX) {
        return false;
    }
    *out = (uint16_t)x;
    return true;
}

bool var_to_int32(const var_t *v, int32_t *out)
{
    int64_t x;

    if (out == NULL || !var_read_i64(v, &x) ||
        x < INT32_MIN || x > INT32_MAX) {
        return false;
    }
    *out = (int32_t)x;
    return true;
}

bool var_to_uint32(const var_t *v, uint32_t *out)
{
    uint64_t x;

    if (out == NULL || !var_read_u64(v, &x) || x > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)x;
    return true;
}

bool var_to_int64(const var_t *v, int64_t *out)
{
    return var_read_i64(v, out);
}

bool var_to_uint64(const var_t *v, uint64_t *out)
{
    return var_read_u64(v, out);
}

bool var_to_float32(const var_t *v, float *out)
{
    double d;

    if (out == NULL || !var_read_f64(v, &d) ||
        d > (double)FLT_MAX || d < -(double)FLT_MAX) {
        return false;
    }
    *out = (float)d;
    return true;
}

bool var_to_float64(const var_t *v, double *out)
{
    return var_read_f64(v, out);
}

/* 严格的十进制输出：空间不足返回 false */
static bool put_u64_text(char *buf, uint32_t size, uint64_t value)
{
    char tmp[21];
    uint32_t n = 0U;
    uint32_t i;

    if (buf == NULL || size == 0U) {
        return false;
    }
    if (value == 0U) {
        if (size < 2U) {
            return false;
        }
        buf[0] = '0';
        buf[1] = '\0';
        return true;
    }
    while (value > 0U && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (uint32_t)(value % 10U));
        value /= 10U;
    }
    if (n + 1U > size) {
        return false;
    }
    for (i = 0U; i < n; i++) {
        buf[i] = tmp[n - 1U - i];
    }
    buf[n] = '\0';
    return true;
}

static bool put_i64_text(char *buf, uint32_t size, int64_t value)
{
    uint64_t mag;

    if (value >= 0) {
        return put_u64_text(buf, size, (uint64_t)value);
    }
    if (buf == NULL || size < 2U) {
        return false;
    }
    buf[0] = '-';
    mag = (uint64_t)(-(value + 1)) + 1U;
    return put_u64_text(&buf[1], size - 1U, mag);
}

static bool put_text(char *buf, uint32_t size, const char *s)
{
    size_t n;

    if (buf == NULL || size == 0U || s == NULL) {
        return false;
    }
    n = strlen(s);
    if (n + 1U > size) {
        return false;
    }
    memcpy(buf, s, n + 1U);
    return true;
}

bool var_to_string(const var_t *v, char *buf, uint32_t size)
{
    if (v == NULL || buf == NULL || size == 0U) {
        return false;
    }
    switch (v->type) {
    case VAR_BOOL:
        return put_text(buf, size, v->value.b ? "true" : "false");
    case VAR_INT8:
        return put_i64_text(buf, size, v->value.i8);
    case VAR_UINT8:
        return put_u64_text(buf, size, v->value.u8);
    case VAR_INT16:
        return put_i64_text(buf, size, v->value.i16);
    case VAR_UINT16:
        return put_u64_text(buf, size, v->value.u16);
    case VAR_INT32:
        return put_i64_text(buf, size, v->value.i32);
    case VAR_UINT32:
        return put_u64_text(buf, size, v->value.u32);
    case VAR_INT64:
        return put_i64_text(buf, size, v->value.i64);
    case VAR_UINT64:
        return put_u64_text(buf, size, v->value.u64);
    case VAR_FLOAT32:
    case VAR_FLOAT64: {
        char *text;
        bool ok;

        if (!isfinite((v->type == VAR_FLOAT32) ? (double)v->value.f32
                                               : v->value.f64)) {
            return false;
        }
        text = var_to_json(v);
        if (text == NULL) {
            return false;
        }
        ok = put_text(buf, size, text);
        var_free_json(text);
        return ok;
    }
    case VAR_STRING:
        return put_text(buf, size, v->value.s);
    default:
        return false;
    }
}

/* ============================================================================
 * 释放
 * ==========================================================================*/

static void var_free_depth(var_t *v, uint32_t depth)
{
    if (v == NULL) {
        return;
    }
    if (depth > VAR_MAX_DEPTH) {
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

/* ============================================================================
 * 深拷贝
 * ==========================================================================*/

static bool var_clone_depth(const var_t *src, var_t *dst, uint32_t depth)
{
    if (dst == NULL) {
        return false;
    }
    var_init(dst);
    if (src == NULL || depth > VAR_MAX_DEPTH) {
        return false;
    }

    switch (src->type) {
    case VAR_NONE:
        return true;

    case VAR_BOOL:
    case VAR_INT8:
    case VAR_UINT8:
    case VAR_INT16:
    case VAR_UINT16:
    case VAR_INT32:
    case VAR_UINT32:
    case VAR_INT64:
    case VAR_UINT64:
    case VAR_FLOAT32:
    case VAR_FLOAT64:
    case VAR_STRING:
        *dst = *src;                    /* 字符串仍为借用 */
        return true;

    case VAR_ARRAY: {
        const vec_t *arr = src->value.arr;
        vec_t *copy;
        uint32_t i;

        if (arr == NULL) {
            return true;                /* NULL 容器等价于空值 */
        }
        copy = (vec_t *)ros_mem_calloc(1U, sizeof(*copy));
        if (copy == NULL) {
            return false;
        }
        if (vec_init(copy, (uint32_t)sizeof(var_t), arr->len) != 0) {
            ros_mem_free(copy);
            return false;
        }
        var_set_array(dst, copy);

        for (i = 0U; i < arr->len; i++) {
            var_t elem;

            if (!var_clone_depth((const var_t *)vec_at(arr, i), &elem,
                                 depth + 1U)) {
                var_free(dst);
                return false;
            }
            if (vec_push(copy, &elem) != 0) {
                var_free(&elem);
                var_free(dst);
                return false;
            }
        }
        return true;
    }

    case VAR_OBJECT: {
        const map_t *obj = src->value.obj;
        map_t *copy;
        map_iter_t it;
        map_item_t item;

        if (obj == NULL) {
            return true;
        }
        copy = (map_t *)ros_mem_calloc(1U, sizeof(*copy));
        if (copy == NULL) {
            return false;
        }
        if (map_init(copy) != 0) {
            ros_mem_free(copy);
            return false;
        }
        var_set_object(dst, copy);

        map_iter_begin(obj, &it);
        while (map_iter_next(&it, &item)) {
            var_t *val = (var_t *)ros_mem_calloc(1U, sizeof(*val));

            if (val == NULL ||
                !var_clone_depth((const var_t *)item.value, val,
                                 depth + 1U)) {
                if (val != NULL) {
                    ros_mem_free(val);
                }
                var_free(dst);
                return false;
            }
            if (!map_put(copy, item.key, val)) {
                var_free(val);
                ros_mem_free(val);
                var_free(dst);
                return false;
            }
        }
        return true;
    }

    default:
        return false;
    }
}

bool var_clone(const var_t *src, var_t *dst)
{
    return var_clone_depth(src, dst, 0U);
}

/* ============================================================================
 * 内部工具
 * ==========================================================================*/

static var_t *var_node_new(void)
{
    return (var_t *)ros_mem_calloc(1U, sizeof(var_t));
}

static map_t *var_object_map(const var_t *v)
{
    if (v == NULL || v->type != VAR_OBJECT) {
        return NULL;
    }
    return v->value.obj;
}

static vec_t *var_array_vec(const var_t *v)
{
    if (v == NULL || v->type != VAR_ARRAY || v->value.arr == NULL) {
        return NULL;
    }
    if (v->value.arr->elem_size != sizeof(var_t)) {
        return NULL;                    /* 元素不是 var_t，无法按此接口操作 */
    }
    return v->value.arr;
}

/*
 * 取得 elem 所有权并写入对象：键已存在时释放旧值再覆盖。
 * 失败时释放 elem；调用者成功后不要再使用 elem。
 */
static bool var_object_put_node(var_t *v, const char *key, var_t *elem)
{
    map_t *m = var_object_map(v);
    void *old_p = NULL;

    if (m == NULL || key == NULL || elem == NULL) {
        var_free(elem);
        ros_mem_free(elem);
        return false;
    }

    (void)map_get(m, key, &old_p);
    if (!map_put(m, key, elem)) {
        var_free(elem);
        ros_mem_free(elem);
        return false;
    }
    if (old_p != NULL) {
        var_t *old = (var_t *)old_p;

        var_free(old);
        ros_mem_free(old);
    }
    return true;
}

/*
 * 取得 elem 所有权并追加到数组。
 * vec 会拷贝 var_t 内容，因此成功后只释放临时外壳。
 * 失败时释放 elem。
 */
static bool var_array_push_node(var_t *v, var_t *elem)
{
    vec_t *arr = var_array_vec(v);

    if (arr == NULL || elem == NULL) {
        var_free(elem);
        ros_mem_free(elem);
        return false;
    }
    if (vec_push(arr, elem) != 0) {
        var_free(elem);
        ros_mem_free(elem);
        return false;
    }
    ros_mem_free(elem);
    return true;
}

/* ============================================================================
 * 对象操作
 * ==========================================================================*/

map_t *var_object_new(var_t *v)
{
    map_t *m;

    if (v == NULL) {
        return NULL;
    }
    var_init(v);
    m = (map_t *)ros_mem_calloc(1U, sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    if (map_init(m) != 0) {
        ros_mem_free(m);
        return NULL;
    }
    var_set_object(v, m);
    return m;
}

bool var_object_put_bool(var_t *v, const char *key, bool value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_bool(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_int8(var_t *v, const char *key, int8_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_int8(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_uint8(var_t *v, const char *key, uint8_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_uint8(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_int16(var_t *v, const char *key, int16_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_int16(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_uint16(var_t *v, const char *key, uint16_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_uint16(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_int32(var_t *v, const char *key, int32_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_int32(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_uint32(var_t *v, const char *key, uint32_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_uint32(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_int64(var_t *v, const char *key, int64_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_int64(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_uint64(var_t *v, const char *key, uint64_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_uint64(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_float32(var_t *v, const char *key, float value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_float32(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_float64(var_t *v, const char *key, double value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_float64(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_str(var_t *v, const char *key, const char *value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_string(elem, value);
    return var_object_put_node(v, key, elem);
}

bool var_object_put_var(var_t *v, const char *key, const var_t *value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    if (!var_clone(value, elem)) {
        ros_mem_free(elem);
        return false;
    }
    return var_object_put_node(v, key, elem);
}

const var_t *var_object_get(const var_t *v, const char *key)
{
    map_t *m = var_object_map(v);

    if (m == NULL || key == NULL) {
        return NULL;
    }
    return (const var_t *)map_get_ptr(m, key);
}

bool var_object_get_bool(const var_t *v, const char *key, bool *out)
{
    return var_get_bool(var_object_get(v, key), out);
}

bool var_object_get_int8(const var_t *v, const char *key, int8_t *out)
{
    return var_get_int8(var_object_get(v, key), out);
}

bool var_object_get_uint8(const var_t *v, const char *key, uint8_t *out)
{
    return var_get_uint8(var_object_get(v, key), out);
}

bool var_object_get_int16(const var_t *v, const char *key, int16_t *out)
{
    return var_get_int16(var_object_get(v, key), out);
}

bool var_object_get_uint16(const var_t *v, const char *key, uint16_t *out)
{
    return var_get_uint16(var_object_get(v, key), out);
}

bool var_object_get_int32(const var_t *v, const char *key, int32_t *out)
{
    return var_get_int32(var_object_get(v, key), out);
}

bool var_object_get_uint32(const var_t *v, const char *key, uint32_t *out)
{
    return var_get_uint32(var_object_get(v, key), out);
}

bool var_object_get_int64(const var_t *v, const char *key, int64_t *out)
{
    return var_get_int64(var_object_get(v, key), out);
}

bool var_object_get_uint64(const var_t *v, const char *key, uint64_t *out)
{
    return var_get_uint64(var_object_get(v, key), out);
}

bool var_object_get_float32(const var_t *v, const char *key, float *out)
{
    return var_get_float32(var_object_get(v, key), out);
}

bool var_object_get_float64(const var_t *v, const char *key, double *out)
{
    return var_get_float64(var_object_get(v, key), out);
}

const char *var_object_get_str(const var_t *v, const char *key)
{
    return var_get_string(var_object_get(v, key));
}

uint32_t var_object_size(const var_t *v)
{
    map_t *m = var_object_map(v);

    return (m != NULL) ? map_size(m) : 0U;
}

bool var_object_has(const var_t *v, const char *key)
{
    map_t *m = var_object_map(v);

    return (m != NULL && key != NULL) ? map_contains(m, key) : false;
}

bool var_object_remove(var_t *v, const char *key)
{
    map_t *m = var_object_map(v);
    void *old_p = NULL;

    if (m == NULL || key == NULL || !map_get(m, key, &old_p)) {
        return false;
    }
    if (!map_remove(m, key)) {
        return false;
    }
    if (old_p != NULL) {
        var_t *old = (var_t *)old_p;

        var_free(old);
        ros_mem_free(old);
    }
    return true;
}

void var_object_clear(var_t *v)
{
    map_t *m = var_object_map(v);
    map_iter_t it;
    map_item_t item;

    if (m == NULL) {
        return;
    }
    map_iter_begin(m, &it);
    while (map_iter_next(&it, &item)) {
        var_t *elem = (var_t *)item.value;

        if (elem != NULL) {
            var_free(elem);
            ros_mem_free(elem);
        }
    }
    map_clear(m);
}

/* ============================================================================
 * 数组操作
 * ==========================================================================*/

vec_t *var_array_new(var_t *v)
{
    vec_t *arr;

    if (v == NULL) {
        return NULL;
    }
    var_init(v);
    arr = (vec_t *)ros_mem_calloc(1U, sizeof(*arr));
    if (arr == NULL) {
        return NULL;
    }
    if (vec_init(arr, (uint32_t)sizeof(var_t), 0U) != 0) {
        ros_mem_free(arr);
        return NULL;
    }
    var_set_array(v, arr);
    return arr;
}

bool var_array_push_bool(var_t *v, bool value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_bool(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_int8(var_t *v, int8_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_int8(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_uint8(var_t *v, uint8_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_uint8(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_int16(var_t *v, int16_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_int16(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_uint16(var_t *v, uint16_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_uint16(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_int32(var_t *v, int32_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_int32(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_uint32(var_t *v, uint32_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_uint32(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_int64(var_t *v, int64_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_int64(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_uint64(var_t *v, uint64_t value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_uint64(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_float32(var_t *v, float value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_float32(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_float64(var_t *v, double value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_float64(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_str(var_t *v, const char *value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    var_set_string(elem, value);
    return var_array_push_node(v, elem);
}

bool var_array_push_var(var_t *v, const var_t *value)
{
    var_t *elem = var_node_new();

    if (elem == NULL) {
        return false;
    }
    if (!var_clone(value, elem)) {
        ros_mem_free(elem);
        return false;
    }
    return var_array_push_node(v, elem);
}

uint32_t var_array_size(const var_t *v)
{
    vec_t *arr = var_array_vec(v);

    return (arr != NULL) ? arr->len : 0U;
}

const var_t *var_array_get(const var_t *v, uint32_t index)
{
    vec_t *arr = var_array_vec(v);

    return (arr != NULL) ? (const var_t *)vec_at(arr, index) : NULL;
}

bool var_array_get_bool(const var_t *v, uint32_t index, bool *out)
{
    return var_get_bool(var_array_get(v, index), out);
}

bool var_array_get_int8(const var_t *v, uint32_t index, int8_t *out)
{
    return var_get_int8(var_array_get(v, index), out);
}

bool var_array_get_uint8(const var_t *v, uint32_t index, uint8_t *out)
{
    return var_get_uint8(var_array_get(v, index), out);
}

bool var_array_get_int16(const var_t *v, uint32_t index, int16_t *out)
{
    return var_get_int16(var_array_get(v, index), out);
}

bool var_array_get_uint16(const var_t *v, uint32_t index, uint16_t *out)
{
    return var_get_uint16(var_array_get(v, index), out);
}

bool var_array_get_int32(const var_t *v, uint32_t index, int32_t *out)
{
    return var_get_int32(var_array_get(v, index), out);
}

bool var_array_get_uint32(const var_t *v, uint32_t index, uint32_t *out)
{
    return var_get_uint32(var_array_get(v, index), out);
}

bool var_array_get_int64(const var_t *v, uint32_t index, int64_t *out)
{
    return var_get_int64(var_array_get(v, index), out);
}

bool var_array_get_uint64(const var_t *v, uint32_t index, uint64_t *out)
{
    return var_get_uint64(var_array_get(v, index), out);
}

bool var_array_get_float32(const var_t *v, uint32_t index, float *out)
{
    return var_get_float32(var_array_get(v, index), out);
}

bool var_array_get_float64(const var_t *v, uint32_t index, double *out)
{
    return var_get_float64(var_array_get(v, index), out);
}

const char *var_array_get_str(const var_t *v, uint32_t index)
{
    return var_get_string(var_array_get(v, index));
}

bool var_array_remove(var_t *v, uint32_t index)
{
    vec_t *arr = var_array_vec(v);
    var_t elem;

    if (arr == NULL || vec_remove(arr, index, &elem) != 0) {
        return false;
    }
    var_free(&elem);
    return true;
}

bool var_array_pop(var_t *v, var_t *out)
{
    vec_t *arr = var_array_vec(v);
    var_t elem;

    if (arr == NULL || arr->len == 0U || vec_pop(arr, &elem) != 0) {
        return false;
    }
    if (out != NULL) {
        *out = elem;
    } else {
        var_free(&elem);
    }
    return true;
}

void var_array_clear(var_t *v)
{
    vec_t *arr = var_array_vec(v);
    uint32_t i;

    if (arr == NULL) {
        return;
    }
    for (i = 0U; i < arr->len; i++) {
        var_free((var_t *)vec_at(arr, i));
    }
    vec_clear(arr);
}

/* ============================================================================
 * 遍历
 * ==========================================================================*/

bool var_array_foreach(const var_t *v, var_array_foreach_fn cb, void *ctx)
{
    vec_t *arr = var_array_vec(v);
    uint32_t i;

    if (arr == NULL || cb == NULL) {
        return false;
    }
    for (i = 0U; i < arr->len; i++) {
        if (!cb(i, (const var_t *)vec_at(arr, i), ctx)) {
            return false;
        }
    }
    return true;
}

typedef struct {
    var_object_foreach_fn cb;
    void                 *ctx;
} var_object_iter_t;

static bool var_object_foreach_item(const map_item_t *item, void *ctx)
{
    var_object_iter_t *it = (var_object_iter_t *)ctx;

    return it->cb(item->key, (const var_t *)item->value, it->ctx);
}

bool var_object_foreach(const var_t *v, var_object_foreach_fn cb, void *ctx)
{
    map_t *obj = var_object_map(v);
    var_object_iter_t it;

    if (obj == NULL || cb == NULL) {
        return false;
    }
    it.cb = cb;
    it.ctx = ctx;
    return map_foreach(obj, var_object_foreach_item, &it);
}
