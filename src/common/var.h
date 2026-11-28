/*
 * var.h - 通用值容器（类型标记 + 联合体）
 * ============================================================================
 *
 * 用联合体承载布尔 / 8~64 位有符号与无符号整数 / 32/64 位浮点 /
 * 字符串 / 数组 / 对象。type 字段标明当前有效成员。
 *
 * 读写接口分两类：
 *   - var_set_xxx / var_get_xxx：严格匹配类型，不做转换；
 *   - var_to_xxx：带类型转换的读取，失败返回 false。
 *
 * 引用与所有权：
 *   - STRING 为借用的 NUL 结尾字符串，var 不负责释放；
 *   - ARRAY（vec_t*）与 OBJECT（map_t*）可通过 var_array_new/var_object_new
 *     或 var_from_json_ex 创建，此时容器与元素由 ros_mem 分配，归 var 所有，
 *     使用结束后调用 var_free() 释放；
 *   - 也可以 set_array/set_object 借用外部容器，此时不要对 var 调用 var_free。
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

/* ============================================================================
 * 类型与基础读写
 * ==========================================================================*/

/* 当前有效成员 */
typedef enum {
    VAR_NONE = 0,   /* 空值（未设置） */
    VAR_BOOL,       /* value.b */
    VAR_INT8,       /* value.i8 */
    VAR_UINT8,      /* value.u8 */
    VAR_INT16,      /* value.i16 */
    VAR_UINT16,     /* value.u16 */
    VAR_INT32,      /* value.i32 */
    VAR_UINT32,     /* value.u32 */
    VAR_INT64,      /* value.i64 */
    VAR_UINT64,     /* value.u64 */
    VAR_FLOAT32,    /* value.f32 */
    VAR_FLOAT64,    /* value.f64 */
    VAR_STRING,     /* value.s，只读 NUL 结尾字符串 */
    VAR_ARRAY,      /* value.arr，vec_t* */
    VAR_OBJECT      /* value.obj，map_t* */
} var_e;

typedef struct {
    var_e type;
    union {
        bool      b;
        int8_t    i8;
        uint8_t   u8;
        int16_t   i16;
        uint16_t  u16;
        int32_t   i32;
        uint32_t  u32;
        int64_t   i64;
        uint64_t  u64;
        float     f32;
        double    f64;
        const char *s;
        vec_t    *arr;
        map_t    *obj;
    } value;
} var_t;

/* 置为空值；v 可为 NULL */
void var_init(var_t *v);

/* 设置值：写入对应成员并更新 type；v 为 NULL 时忽略 */
void var_set_bool(var_t *v, bool value);
void var_set_int8(var_t *v, int8_t value);
void var_set_uint8(var_t *v, uint8_t value);
void var_set_int16(var_t *v, int16_t value);
void var_set_uint16(var_t *v, uint16_t value);
void var_set_int32(var_t *v, int32_t value);
void var_set_uint32(var_t *v, uint32_t value);
void var_set_int64(var_t *v, int64_t value);
void var_set_uint64(var_t *v, uint64_t value);
void var_set_float32(var_t *v, float value);
void var_set_float64(var_t *v, double value);
void var_set_string(var_t *v, const char *value);   /* NULL 视为清空 */
void var_set_array(var_t *v, vec_t *value);         /* NULL 视为清空 */
void var_set_object(var_t *v, map_t *value);        /* NULL 视为清空 */

/* 是否为空值；v 为 NULL 时返回 true */
bool var_is_none(const var_t *v);

/* 严格读取：类型完全匹配且 out 非空时写入并返回 true，否则 false */
bool var_get_bool(const var_t *v, bool *out);
bool var_get_int8(const var_t *v, int8_t *out);
bool var_get_uint8(const var_t *v, uint8_t *out);
bool var_get_int16(const var_t *v, int16_t *out);
bool var_get_uint16(const var_t *v, uint16_t *out);
bool var_get_int32(const var_t *v, int32_t *out);
bool var_get_uint32(const var_t *v, uint32_t *out);
bool var_get_int64(const var_t *v, int64_t *out);
bool var_get_uint64(const var_t *v, uint64_t *out);
bool var_get_float32(const var_t *v, float *out);
bool var_get_float64(const var_t *v, double *out);

/* 引用读取：类型不匹配返回 NULL */
const char *var_get_string(const var_t *v);
vec_t *var_get_array(const var_t *v);
map_t *var_get_object(const var_t *v);

/* ============================================================================
 * 类型转换
 *
 * 支持标量之间的转换（bool / 各宽度整数 / 浮点 / 字符串）。
 * 数值转换失败（越界、NaN/Inf、字符串无法解析）返回 false 且不写 out。
 * var_to_bool：数值 != 0 为 true；字符串优先按布尔文本解析，否则非空为 true。
 * var_to_string：按十进制输出；缓冲区不足返回 false。
 * ==========================================================================*/

bool var_to_bool(const var_t *v, bool *out);
bool var_to_int8(const var_t *v, int8_t *out);
bool var_to_uint8(const var_t *v, uint8_t *out);
bool var_to_int16(const var_t *v, int16_t *out);
bool var_to_uint16(const var_t *v, uint16_t *out);
bool var_to_int32(const var_t *v, int32_t *out);
bool var_to_uint32(const var_t *v, uint32_t *out);
bool var_to_int64(const var_t *v, int64_t *out);
bool var_to_uint64(const var_t *v, uint64_t *out);
bool var_to_float32(const var_t *v, float *out);
bool var_to_float64(const var_t *v, double *out);
bool var_to_string(const var_t *v, char *buf, uint32_t size);

/* ============================================================================
 * 生命周期
 * ==========================================================================*/

/*
 * 递归释放复杂类型并置为空值：
 *   - ARRAY：逐元素 var_free，再 vec_deinit + 释放 vec_t 本身；
 *   - OBJECT：逐 value（var_t*）var_free 并释放，再 map_deinit + 释放 map_t；
 *   - 标量与字符串不释放（字符串是借用引用）。
 * 约定：容器与元素均由 ros_mem 分配、归该 var 所有；只是借用的容器
 * 不要调用 var_free。嵌套超过 8 层时不再深入（防止循环引用）。
 * 调用后 v 置为 VAR_NONE。v 可为 NULL。
 */
void var_free(var_t *v);

/*
 * 深拷贝：标量/字符串直接复制（字符串仍为借用引用），
 * ARRAY/OBJECT 递归分配新容器与元素，所有权归 dst，可用 var_free 释放。
 * 返回 true 成功；src/dst 为 NULL、深度超限或内存不足返回 false
 * （dst 置为 VAR_NONE，已分配的临时内存全部释放）。
 */
bool var_clone(const var_t *src, var_t *dst);

/* ============================================================================
 * 对象（VAR_OBJECT）
 *
 * 容器与元素由 ros_mem 分配，所有权归 var，可直接 var_free 释放。
 * ==========================================================================*/

/*
 * 把 v 初始化为空对象并返回内部 map；v 原值会被直接覆盖（不释放旧值）。
 * 失败返回 NULL，此时 v 为 VAR_NONE。
 */
map_t *var_object_new(var_t *v);

/* 写入键值；键已存在时释放旧值并覆盖。失败返回 false */
bool var_object_put_bool(var_t *v, const char *key, bool value);
bool var_object_put_int8(var_t *v, const char *key, int8_t value);
bool var_object_put_uint8(var_t *v, const char *key, uint8_t value);
bool var_object_put_int16(var_t *v, const char *key, int16_t value);
bool var_object_put_uint16(var_t *v, const char *key, uint16_t value);
bool var_object_put_int32(var_t *v, const char *key, int32_t value);
bool var_object_put_uint32(var_t *v, const char *key, uint32_t value);
bool var_object_put_int64(var_t *v, const char *key, int64_t value);
bool var_object_put_uint64(var_t *v, const char *key, uint64_t value);
bool var_object_put_float32(var_t *v, const char *key, float value);
bool var_object_put_float64(var_t *v, const char *key, double value);
bool var_object_put_str(var_t *v, const char *key, const char *value);

/* 深拷贝 value 后写入（value 可为标量或容器） */
bool var_object_put_var(var_t *v, const char *key, const var_t *value);

/* 读取成员；v 不是对象或键不存在返回 NULL */
const var_t *var_object_get(const var_t *v, const char *key);

/* 严格读取成员，类型不符或不存在返回 false */
bool var_object_get_bool(const var_t *v, const char *key, bool *out);
bool var_object_get_int8(const var_t *v, const char *key, int8_t *out);
bool var_object_get_uint8(const var_t *v, const char *key, uint8_t *out);
bool var_object_get_int16(const var_t *v, const char *key, int16_t *out);
bool var_object_get_uint16(const var_t *v, const char *key, uint16_t *out);
bool var_object_get_int32(const var_t *v, const char *key, int32_t *out);
bool var_object_get_uint32(const var_t *v, const char *key, uint32_t *out);
bool var_object_get_int64(const var_t *v, const char *key, int64_t *out);
bool var_object_get_uint64(const var_t *v, const char *key, uint64_t *out);
bool var_object_get_float32(const var_t *v, const char *key, float *out);
bool var_object_get_float64(const var_t *v, const char *key, double *out);

/* 读取字符串成员；类型不符返回 NULL */
const char *var_object_get_str(const var_t *v, const char *key);

/* 成员个数 / 是否包含键 */
uint32_t var_object_size(const var_t *v);
bool var_object_has(const var_t *v, const char *key);

/* 删除成员（释放值）；不存在返回 false */
bool var_object_remove(var_t *v, const char *key);

/* 清空全部成员（保留对象容器本身） */
void var_object_clear(var_t *v);

/* ============================================================================
 * 数组（VAR_ARRAY）
 *
 * vec 元素固定为 var_t，容器与元素由 ros_mem 分配，所有权归 var。
 * ==========================================================================*/

/*
 * 把 v 初始化为空数组并返回内部 vec；v 原值会被直接覆盖（不释放旧值）。
 * 失败返回 NULL，此时 v 为 VAR_NONE。
 */
vec_t *var_array_new(var_t *v);

/* 追加元素；失败返回 false */
bool var_array_push_bool(var_t *v, bool value);
bool var_array_push_int8(var_t *v, int8_t value);
bool var_array_push_uint8(var_t *v, uint8_t value);
bool var_array_push_int16(var_t *v, int16_t value);
bool var_array_push_uint16(var_t *v, uint16_t value);
bool var_array_push_int32(var_t *v, int32_t value);
bool var_array_push_uint32(var_t *v, uint32_t value);
bool var_array_push_int64(var_t *v, int64_t value);
bool var_array_push_uint64(var_t *v, uint64_t value);
bool var_array_push_float32(var_t *v, float value);
bool var_array_push_float64(var_t *v, double value);
bool var_array_push_str(var_t *v, const char *value);

/* 深拷贝 value 后追加（value 可为标量或容器） */
bool var_array_push_var(var_t *v, const var_t *value);

/* 元素个数；v 不是数组返回 0 */
uint32_t var_array_size(const var_t *v);

/* 按下标读取；越界或 v 不是数组返回 NULL */
const var_t *var_array_get(const var_t *v, uint32_t index);

/* 严格读取元素，类型不符或越界返回 false */
bool var_array_get_bool(const var_t *v, uint32_t index, bool *out);
bool var_array_get_int8(const var_t *v, uint32_t index, int8_t *out);
bool var_array_get_uint8(const var_t *v, uint32_t index, uint8_t *out);
bool var_array_get_int16(const var_t *v, uint32_t index, int16_t *out);
bool var_array_get_uint16(const var_t *v, uint32_t index, uint16_t *out);
bool var_array_get_int32(const var_t *v, uint32_t index, int32_t *out);
bool var_array_get_uint32(const var_t *v, uint32_t index, uint32_t *out);
bool var_array_get_int64(const var_t *v, uint32_t index, int64_t *out);
bool var_array_get_uint64(const var_t *v, uint32_t index, uint64_t *out);
bool var_array_get_float32(const var_t *v, uint32_t index, float *out);
bool var_array_get_float64(const var_t *v, uint32_t index, double *out);

/* 按下标读取字符串元素；类型不符返回 NULL */
const char *var_array_get_str(const var_t *v, uint32_t index);

/* 删除指定下标元素并释放；越界返回 false */
bool var_array_remove(var_t *v, uint32_t index);

/* 弹出末尾元素；out 非 NULL 时转移所有权，否则直接释放 */
bool var_array_pop(var_t *v, var_t *out);

/* 清空全部元素（保留数组容器本身） */
void var_array_clear(var_t *v);

/* ============================================================================
 * 遍历
 *
 * 回调返回 false 表示提前停止。函数返回 true 表示完整遍历（含空容器），
 * 返回 false 表示参数非法或回调中途停止。
 * ==========================================================================*/

/* 数组遍历：index 从 0 开始 */
typedef bool (*var_array_foreach_fn)(uint32_t index, const var_t *elem,
                                     void *ctx);
bool var_array_foreach(const var_t *v, var_array_foreach_fn cb, void *ctx);

/* 对象遍历：按键升序，value 可能为 NULL */
typedef bool (*var_object_foreach_fn)(const char *key, const var_t *value,
                                      void *ctx);
bool var_object_foreach(const var_t *v, var_object_foreach_fn cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VAR_H */
