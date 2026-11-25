/*
 * var.h - 通用值容器（类型标记 + 联合体）
 *
 * 用联合体承载布尔 / 整数 / 单精度浮点 / 字符串 / 数组 / 对象，
 * type 字段标明当前有效的成员，读写都必须先确认类型。
 *
 * 字符串、数组、对象都是只读/借用引用（不拷贝、不释放）：
 *   - STRING 为 NUL 结尾的 const char*
 *   - ARRAY  为 vec_t*
 *   - OBJECT 为 map_t*
 * 生命周期由调用者保证；对应 set_xxx(NULL) 视为清空。
 *
 * var_t 大小随指针宽度：32 位平台 8 字节，64 位平台 16 字节。
 */

#ifndef VAR_H
#define VAR_H

#include <stdbool.h>
#include <stdint.h>

#include "vec.h"
#include "map.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 当前有效成员 */
typedef enum {
    VAR_NONE = 0,   /* 空值（未设置） */
    VAR_BOOL,       /* value.b */
    VAR_INT,        /* value.i，有符号 32 位 */
    VAR_FLOAT,      /* value.f，单精度 */
    VAR_STRING,     /* value.s，只读 NUL 结尾字符串 */
    VAR_ARRAY,      /* value.arr，vec_t*（借用） */
    VAR_OBJECT      /* value.obj，map_t*（借用） */
} var_e;

typedef struct {
    var_e type;
    union {
        bool        b;
        int32_t     i;
        float       f;
        const char *s;
        vec_t      *arr;
        map_t      *obj;
    } value;
} var_t;

/* 置为空值；v 可为 NULL */
void var_init(var_t *v);

/* 设置值：写入对应成员并更新 type；v 为 NULL 时忽略 */
void var_set_bool(var_t *v, bool value);
void var_set_int(var_t *v, int32_t value);
void var_set_float(var_t *v, float value);
void var_set_string(var_t *v, const char *value);   /* NULL 视为清空 */
void var_set_array(var_t *v, vec_t *value);         /* NULL 视为清空 */
void var_set_object(var_t *v, map_t *value);        /* NULL 视为清空 */

/* 是否为空值；v 为 NULL 时返回 true */
bool var_is_none(const var_t *v);

/* 读取值：类型匹配且 out 非空时写入并返回 true，否则返回 false */
bool var_get_bool(const var_t *v, bool *out);
bool var_get_int(const var_t *v, int32_t *out);
bool var_get_float(const var_t *v, float *out);

/* 引用读取：类型不匹配返回 NULL */
const char *var_get_string(const var_t *v);
vec_t *var_get_array(const var_t *v);
map_t *var_get_object(const var_t *v);

#ifdef __cplusplus
}
#endif

#endif /* VAR_H */
