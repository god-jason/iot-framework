/**
 * json.h - cJSON 的宏封装
 *
 * 目的：把常用的 JSON 解析/生成操作收敛为一组宏，减少样板代码。
 * 底层完全基于 vendor/cjson 的 cJSON API，不引入额外内存管理。
 *
 * 使用示例：
 *
 *   typedef struct {
 *       int id;
 *       char name[32];
 *       bool enabled;
 *       float temperature;
 *       float values[4];
 *   } device_t;
 *
 *   void test(void)
 *   {
 *       const char *text =
 *           "{\"id\":1001,\"name\":\"sensor01\","
 *           "\"enabled\":true,\"temperature\":25.5,"
 *           "\"values\":[1.1,2.2,3.3,4.4]}";
 *
 *       device_t dev = {0};
 *
 *       // JSON -> struct（成员名与键名不同，使用 _NAME 变体）
 *       cJSON *root = JSON_PARSE_TEXT(text);
 *       if (root) {
 *           JSON_PARSE_INT_NAME(root, dev.id, "id");
 *           JSON_PARSE_STRING_NAME(root, dev.name, "name", sizeof(dev.name));
 *           JSON_PARSE_BOOL_NAME(root, dev.enabled, "enabled");
 *           JSON_PARSE_FLOAT_NAME(root, dev.temperature, "temperature");
 *           JSON_PARSE_ARRAY_FLOAT_NAME(root, dev.values, "values", 4);
 *           JSON_DELETE(root);
 *       }
 *
 *       // struct -> JSON
 *       root = JSON_CREATE();
 *       if (root) {
 *           JSON_ADD_INT_NAME(root, dev.id, "id");
 *           JSON_ADD_STRING_NAME(root, dev.name, "name");
 *           JSON_ADD_BOOL_NAME(root, dev.enabled, "enabled");
 *           JSON_ADD_FLOAT_NAME(root, dev.temperature, "temperature");
 *           JSON_ADD_ARRAY_FLOAT_NAME(root, dev.values, "values", 4);
 *
 *           char *output = JSON_PRINT(root);
 *           if (output) {
 *               JSON_FREE_STRING(output);
 *           }
 *           JSON_DELETE(root);
 *       }
 *   }
 *
 * 注意：
 *   - 不带 _NAME 的宏直接用变量名作为 JSON 键（#member），
 *     因此适用于局部变量；结构体成员请使用 _NAME 变体；
 *   - 宏中的 json/key 参数必须是 cJSON 对象与字符串键；
 *   - PARSE_* 系列在字段不存在或类型不符时保持目标变量原值不变；
 *   - ADD/SET 系列返回 cJSON 节点指针，失败时为 NULL；
 *   - 所有解析出的字符串由 cJSON 持有，JSON_DELETE 后不可再使用。
 */
#ifndef JSON_H
#define JSON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 基础错误码
 * 目前主要用于调用方自定义返回值，宏本身不返回这些错误码。
 * ============================================================ */

#define JSON_OK                 0       /* 成功 */
#define JSON_ERR_NULL          -1       /* 空指针 */
#define JSON_ERR_TYPE          -2       /* 类型不匹配 */
#define JSON_ERR_MEMORY        -3       /* 内存不足 */
#define JSON_ERR_RANGE         -4       /* 数值越界 */
#define JSON_ERR_NOT_FOUND     -5       /* 字段不存在 */
#define JSON_ERR_FORMAT        -6       /* 文本格式错误 */

/* ============================================================
 * 基础类型判断
 * ============================================================ */

/* 是否为 null 或空指针 */
#define JSON_IS_NULL(v) \
    ((v) == NULL || cJSON_IsNull(v))

/* 是否为布尔值 */
#define JSON_IS_BOOL(v) \
    (cJSON_IsTrue(v) || cJSON_IsFalse(v))

/* 是否为数字 */
#define JSON_IS_NUMBER(v) \
    (cJSON_IsNumber(v))

/* 是否为字符串 */
#define JSON_IS_STRING(v) \
    (cJSON_IsString(v))

/* 是否为数组 */
#define JSON_IS_ARRAY(v) \
    (cJSON_IsArray(v))

/* 是否为对象 */
#define JSON_IS_OBJECT(v) \
    (cJSON_IsObject(v))

/* ============================================================
 * 字段查找与存在性
 * ============================================================ */

/* 按大小写敏感的方式查找字段，返回 cJSON*（找不到为 NULL） */
#define JSON_GET(json, key) \
    cJSON_GetObjectItemCaseSensitive((json), (key))

/* 字段是否存在且非 null */
#define JSON_HAS(json, key) \
    (!JSON_IS_NULL(JSON_GET((json), (key))))

/* 读取数组字段长度；非数组或字段缺失时 value 保持不变 */
#define JSON_ARRAY_SIZE(json, key, value) do { \
    cJSON *_arr = JSON_GET((json), (key)); \
    if (cJSON_IsArray(_arr)) { \
        (value) = cJSON_GetArraySize(_arr); \
    } \
} while (0)

/* ============================================================
 * 字段读取（显式 key）
 * ============================================================ */

/* 读取有符号整数（取 cJSON 的 valueint） */
#define JSON_GET_INT(json, key, value) do { \
    cJSON *_v = JSON_GET((json), (key)); \
    if (cJSON_IsNumber(_v)) { \
        (value) = _v->valueint; \
    } \
} while (0)

/* 读取无符号整数，负值忽略 */
#define JSON_GET_UINT(json, key, value) do { \
    cJSON *_v = JSON_GET((json), (key)); \
    if (cJSON_IsNumber(_v) && _v->valuedouble >= 0) { \
        (value) = (unsigned int)_v->valuedouble; \
    } \
} while (0)

/* 读取 64 位有符号整数 */
#define JSON_GET_INT64(json, key, value) do { \
    cJSON *_v = JSON_GET((json), (key)); \
    if (cJSON_IsNumber(_v)) { \
        (value) = (int64_t)_v->valuedouble; \
    } \
} while (0)

/* 读取 64 位无符号整数，负值忽略 */
#define JSON_GET_UINT64(json, key, value) do { \
    cJSON *_v = JSON_GET((json), (key)); \
    if (cJSON_IsNumber(_v) && _v->valuedouble >= 0) { \
        (value) = (uint64_t)_v->valuedouble; \
    } \
} while (0)

/* 读取 float */
#define JSON_GET_FLOAT(json, key, value) do { \
    cJSON *_v = JSON_GET((json), (key)); \
    if (cJSON_IsNumber(_v)) { \
        (value) = (float)_v->valuedouble; \
    } \
} while (0)

/* 读取 double */
#define JSON_GET_DOUBLE(json, key, value) do { \
    cJSON *_v = JSON_GET((json), (key)); \
    if (cJSON_IsNumber(_v)) { \
        (value) = _v->valuedouble; \
    } \
} while (0)

/* 读取布尔值 */
#define JSON_GET_BOOL(json, key, value) do { \
    cJSON *_v = JSON_GET((json), (key)); \
    if (cJSON_IsBool(_v)) { \
        (value) = cJSON_IsTrue(_v); \
    } \
} while (0)

/* 读取字符串并截断到 size-1；size 必须大于 0 */
#define JSON_GET_STRING(json, key, value, size) do { \
    cJSON *_v = JSON_GET((json), (key)); \
    if (cJSON_IsString(_v) && _v->valuestring != NULL && (size) > 0) { \
        strncpy((value), _v->valuestring, (size) - 1); \
        (value)[(size) - 1] = '\0'; \
    } \
} while (0)

/* ============================================================
 * 字段设置（显式 key）
 * ============================================================ */

/* 写入有符号整数 */
#define JSON_SET_INT(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

/* 写入无符号整数 */
#define JSON_SET_UINT(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

/* 写入 64 位有符号整数（受 double 精度限制） */
#define JSON_SET_INT64(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

/* 写入 64 位无符号整数（受 double 精度限制） */
#define JSON_SET_UINT64(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

/* 写入 float */
#define JSON_SET_FLOAT(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

/* 写入 double */
#define JSON_SET_DOUBLE(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

/* 写入布尔值 */
#define JSON_SET_BOOL(json, key, value) \
    cJSON_AddBoolToObject((json), (key), (value))

/* 写入字符串 */
#define JSON_SET_STRING(json, key, value) \
    cJSON_AddStringToObject((json), (key), (value))

/* ============================================================
 * JSON -> struct 字段解析（字段名即成员名）
 *
 * 用法：JSON_PARSE_INT(root, dev.id);  // 读取 "id"
 * ============================================================ */

/* 解析 int */
#define JSON_PARSE_INT(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v)) { \
        (member) = _v->valueint; \
    } \
} while (0)

/* 解析 unsigned int，负值忽略 */
#define JSON_PARSE_UINT(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v) && _v->valuedouble >= 0) { \
        (member) = (unsigned int)_v->valuedouble; \
    } \
} while (0)

/* 解析 int8，越界忽略 */
#define JSON_PARSE_INT8(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= INT8_MIN && \
        _v->valuedouble <= INT8_MAX) { \
        (member) = (int8_t)_v->valueint; \
    } \
} while (0)

/* 解析 uint8，越界忽略 */
#define JSON_PARSE_UINT8(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= 0 && \
        _v->valuedouble <= UINT8_MAX) { \
        (member) = (uint8_t)_v->valueint; \
    } \
} while (0)

/* 解析 int16，越界忽略 */
#define JSON_PARSE_INT16(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= INT16_MIN && \
        _v->valuedouble <= INT16_MAX) { \
        (member) = (int16_t)_v->valueint; \
    } \
} while (0)

/* 解析 uint16，越界忽略 */
#define JSON_PARSE_UINT16(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= 0 && \
        _v->valuedouble <= UINT16_MAX) { \
        (member) = (uint16_t)_v->valueint; \
    } \
} while (0)

/* 解析 int32，越界忽略 */
#define JSON_PARSE_INT32(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= INT32_MIN && \
        _v->valuedouble <= INT32_MAX) { \
        (member) = (int32_t)_v->valuedouble; \
    } \
} while (0)

/* 解析 uint32，越界忽略 */
#define JSON_PARSE_UINT32(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= 0 && \
        _v->valuedouble <= UINT32_MAX) { \
        (member) = (uint32_t)_v->valuedouble; \
    } \
} while (0)

/* 解析 int64（double 精度范围内） */
#define JSON_PARSE_INT64(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= -9223372036854775808.0 && \
        _v->valuedouble < 9223372036854775808.0) { \
        (member) = (int64_t)_v->valuedouble; \
    } \
} while (0)

/* 解析 uint64（double 精度范围内） */
#define JSON_PARSE_UINT64(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= 0 && \
        _v->valuedouble < 18446744073709551616.0) { \
        (member) = (uint64_t)_v->valuedouble; \
    } \
} while (0)

/* 解析 float */
#define JSON_PARSE_FLOAT(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v)) { \
        (member) = (float)_v->valuedouble; \
    } \
} while (0)

/* 解析 double */
#define JSON_PARSE_DOUBLE(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsNumber(_v)) { \
        (member) = _v->valuedouble; \
    } \
} while (0)

/* 解析 bool */
#define JSON_PARSE_BOOL(json, member) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsBool(_v)) { \
        (member) = cJSON_IsTrue(_v); \
    } \
} while (0)

/* 解析字符串并截断到 size-1；size 必须大于 0 */
#define JSON_PARSE_STRING(json, member, size) do { \
    cJSON *_v = JSON_GET((json), #member); \
    if (cJSON_IsString(_v) && _v->valuestring != NULL && (size) > 0) { \
        strncpy((member), _v->valuestring, (size) - 1); \
        (member)[(size) - 1] = '\0'; \
    } \
} while (0)

/* ============================================================
 * struct -> JSON 字段序列化（字段名即成员名）
 * ============================================================ */

/* 写入 int */
#define JSON_ADD_INT(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

/* 写入 unsigned int */
#define JSON_ADD_UINT(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

/* 写入 int64 */
#define JSON_ADD_INT64(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

/* 写入 uint64 */
#define JSON_ADD_UINT64(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

/* 写入 float */
#define JSON_ADD_FLOAT(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

/* 写入 double */
#define JSON_ADD_DOUBLE(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

/* 写入 bool */
#define JSON_ADD_BOOL(json, member) \
    cJSON_AddBoolToObject((json), #member, (member))

/* 写入字符串 */
#define JSON_ADD_STRING(json, member) \
    cJSON_AddStringToObject((json), #member, (member))

/* ============================================================
 * 数组解析（按成员名读取 JSON 数组）
 * ============================================================ */

/* 解析 int 数组，最多 count 个元素 */
#define JSON_PARSE_ARRAY_INT(json, member, count) do { \
    cJSON *_arr = JSON_GET((json), #member); \
    if (cJSON_IsArray(_arr)) { \
        size_t _n = cJSON_GetArraySize(_arr); \
        if (_n > (count)) _n = (count); \
        for (size_t _i = 0; _i < _n; _i++) { \
            cJSON *_v = cJSON_GetArrayItem(_arr, (int)_i); \
            if (cJSON_IsNumber(_v)) { \
                (member)[_i] = _v->valueint; \
            } \
        } \
    } \
} while (0)

/* 解析 float 数组 */
#define JSON_PARSE_ARRAY_FLOAT(json, member, count) do { \
    cJSON *_arr = JSON_GET((json), #member); \
    if (cJSON_IsArray(_arr)) { \
        size_t _n = cJSON_GetArraySize(_arr); \
        if (_n > (count)) _n = (count); \
        for (size_t _i = 0; _i < _n; _i++) { \
            cJSON *_v = cJSON_GetArrayItem(_arr, (int)_i); \
            if (cJSON_IsNumber(_v)) { \
                (member)[_i] = (float)_v->valuedouble; \
            } \
        } \
    } \
} while (0)

/* 解析 double 数组 */
#define JSON_PARSE_ARRAY_DOUBLE(json, member, count) do { \
    cJSON *_arr = JSON_GET((json), #member); \
    if (cJSON_IsArray(_arr)) { \
        size_t _n = cJSON_GetArraySize(_arr); \
        if (_n > (count)) _n = (count); \
        for (size_t _i = 0; _i < _n; _i++) { \
            cJSON *_v = cJSON_GetArrayItem(_arr, (int)_i); \
            if (cJSON_IsNumber(_v)) { \
                (member)[_i] = _v->valuedouble; \
            } \
        } \
    } \
} while (0)

/* 解析 bool 数组 */
#define JSON_PARSE_ARRAY_BOOL(json, member, count) do { \
    cJSON *_arr = JSON_GET((json), #member); \
    if (cJSON_IsArray(_arr)) { \
        size_t _n = cJSON_GetArraySize(_arr); \
        if (_n > (count)) _n = (count); \
        for (size_t _i = 0; _i < _n; _i++) { \
            cJSON *_v = cJSON_GetArrayItem(_arr, (int)_i); \
            if (cJSON_IsBool(_v)) { \
                (member)[_i] = cJSON_IsTrue(_v); \
            } \
        } \
    } \
} while (0)

/* 按指定 key 解析 int 数组 */
#define JSON_PARSE_ARRAY_INT_NAME(json, member, name, count) do { \
    cJSON *_arr = JSON_GET((json), (name)); \
    if (cJSON_IsArray(_arr)) { \
        size_t _n = cJSON_GetArraySize(_arr); \
        if (_n > (count)) _n = (count); \
        for (size_t _i = 0; _i < _n; _i++) { \
            cJSON *_v = cJSON_GetArrayItem(_arr, (int)_i); \
            if (cJSON_IsNumber(_v)) { \
                (member)[_i] = _v->valueint; \
            } \
        } \
    } \
} while (0)

/* 按指定 key 解析 float 数组 */
#define JSON_PARSE_ARRAY_FLOAT_NAME(json, member, name, count) do { \
    cJSON *_arr = JSON_GET((json), (name)); \
    if (cJSON_IsArray(_arr)) { \
        size_t _n = cJSON_GetArraySize(_arr); \
        if (_n > (count)) _n = (count); \
        for (size_t _i = 0; _i < _n; _i++) { \
            cJSON *_v = cJSON_GetArrayItem(_arr, (int)_i); \
            if (cJSON_IsNumber(_v)) { \
                (member)[_i] = (float)_v->valuedouble; \
            } \
        } \
    } \
} while (0)

/* 按指定 key 解析 double 数组 */
#define JSON_PARSE_ARRAY_DOUBLE_NAME(json, member, name, count) do { \
    cJSON *_arr = JSON_GET((json), (name)); \
    if (cJSON_IsArray(_arr)) { \
        size_t _n = cJSON_GetArraySize(_arr); \
        if (_n > (count)) _n = (count); \
        for (size_t _i = 0; _i < _n; _i++) { \
            cJSON *_v = cJSON_GetArrayItem(_arr, (int)_i); \
            if (cJSON_IsNumber(_v)) { \
                (member)[_i] = _v->valuedouble; \
            } \
        } \
    } \
} while (0)

/* 按指定 key 解析 bool 数组 */
#define JSON_PARSE_ARRAY_BOOL_NAME(json, member, name, count) do { \
    cJSON *_arr = JSON_GET((json), (name)); \
    if (cJSON_IsArray(_arr)) { \
        size_t _n = cJSON_GetArraySize(_arr); \
        if (_n > (count)) _n = (count); \
        for (size_t _i = 0; _i < _n; _i++) { \
            cJSON *_v = cJSON_GetArrayItem(_arr, (int)_i); \
            if (cJSON_IsBool(_v)) { \
                (member)[_i] = cJSON_IsTrue(_v); \
            } \
        } \
    } \
} while (0)

/* ============================================================
 * 数组序列化（成员名作为 JSON 键）
 * ============================================================ */

/* 写入 int 数组 */
#define JSON_ADD_ARRAY_INT(json, member, count) do { \
    cJSON *_arr = cJSON_CreateArray(); \
    if (_arr) { \
        for (size_t _i = 0; _i < (count); _i++) { \
            cJSON_AddItemToArray(_arr, \
                cJSON_CreateNumber((double)(member)[_i])); \
        } \
        cJSON_AddItemToObject((json), #member, _arr); \
    } \
} while (0)

/* 写入 float 数组 */
#define JSON_ADD_ARRAY_FLOAT(json, member, count) do { \
    cJSON *_arr = cJSON_CreateArray(); \
    if (_arr) { \
        for (size_t _i = 0; _i < (count); _i++) { \
            cJSON_AddItemToArray(_arr, \
                cJSON_CreateNumber((double)(member)[_i])); \
        } \
        cJSON_AddItemToObject((json), #member, _arr); \
    } \
} while (0)

/* 写入 double 数组 */
#define JSON_ADD_ARRAY_DOUBLE(json, member, count) do { \
    cJSON *_arr = cJSON_CreateArray(); \
    if (_arr) { \
        for (size_t _i = 0; _i < (count); _i++) { \
            cJSON_AddItemToArray(_arr, \
                cJSON_CreateNumber((double)(member)[_i])); \
        } \
        cJSON_AddItemToObject((json), #member, _arr); \
    } \
} while (0)

/* 写入 bool 数组 */
#define JSON_ADD_ARRAY_BOOL(json, member, count) do { \
    cJSON *_arr = cJSON_CreateArray(); \
    if (_arr) { \
        for (size_t _i = 0; _i < (count); _i++) { \
            cJSON_AddItemToArray(_arr, \
                cJSON_CreateBool((member)[_i] ? 1 : 0)); \
        } \
        cJSON_AddItemToObject((json), #member, _arr); \
    } \
} while (0)

/* 按指定 key 写入 int 数组 */
#define JSON_ADD_ARRAY_INT_NAME(json, member, name, count) do { \
    cJSON *_arr = cJSON_CreateArray(); \
    if (_arr) { \
        for (size_t _i = 0; _i < (count); _i++) { \
            cJSON_AddItemToArray(_arr, \
                cJSON_CreateNumber((double)(member)[_i])); \
        } \
        cJSON_AddItemToObject((json), (name), _arr); \
    } \
} while (0)

/* 按指定 key 写入 float 数组 */
#define JSON_ADD_ARRAY_FLOAT_NAME(json, member, name, count) do { \
    cJSON *_arr = cJSON_CreateArray(); \
    if (_arr) { \
        for (size_t _i = 0; _i < (count); _i++) { \
            cJSON_AddItemToArray(_arr, \
                cJSON_CreateNumber((double)(member)[_i])); \
        } \
        cJSON_AddItemToObject((json), (name), _arr); \
    } \
} while (0)

/* 按指定 key 写入 double 数组 */
#define JSON_ADD_ARRAY_DOUBLE_NAME(json, member, name, count) do { \
    cJSON *_arr = cJSON_CreateArray(); \
    if (_arr) { \
        for (size_t _i = 0; _i < (count); _i++) { \
            cJSON_AddItemToArray(_arr, \
                cJSON_CreateNumber((double)(member)[_i])); \
        } \
        cJSON_AddItemToObject((json), (name), _arr); \
    } \
} while (0)

/* 按指定 key 写入 bool 数组 */
#define JSON_ADD_ARRAY_BOOL_NAME(json, member, name, count) do { \
    cJSON *_arr = cJSON_CreateArray(); \
    if (_arr) { \
        for (size_t _i = 0; _i < (count); _i++) { \
            cJSON_AddItemToArray(_arr, \
                cJSON_CreateBool((member)[_i] ? 1 : 0)); \
        } \
        cJSON_AddItemToObject((json), (name), _arr); \
    } \
} while (0)

/* ============================================================
 * 字段别名（字段名与成员名不同）
 * ============================================================ */

/* 按指定 key 解析 int */
#define JSON_PARSE_INT_NAME(json, member, name) do { \
    cJSON *_v = JSON_GET((json), (name)); \
    if (cJSON_IsNumber(_v)) { \
        (member) = _v->valueint; \
    } \
} while (0)

/* 按指定 key 解析 uint */
#define JSON_PARSE_UINT_NAME(json, member, name) do { \
    cJSON *_v = JSON_GET((json), (name)); \
    if (cJSON_IsNumber(_v) && _v->valuedouble >= 0) { \
        (member) = (unsigned int)_v->valuedouble; \
    } \
} while (0)

/* 按指定 key 解析 float */
#define JSON_PARSE_FLOAT_NAME(json, member, name) do { \
    cJSON *_v = JSON_GET((json), (name)); \
    if (cJSON_IsNumber(_v)) { \
        (member) = (float)_v->valuedouble; \
    } \
} while (0)

/* 按指定 key 解析 double */
#define JSON_PARSE_DOUBLE_NAME(json, member, name) do { \
    cJSON *_v = JSON_GET((json), (name)); \
    if (cJSON_IsNumber(_v)) { \
        (member) = _v->valuedouble; \
    } \
} while (0)

/* 按指定 key 解析 bool */
#define JSON_PARSE_BOOL_NAME(json, member, name) do { \
    cJSON *_v = JSON_GET((json), (name)); \
    if (cJSON_IsBool(_v)) { \
        (member) = cJSON_IsTrue(_v); \
    } \
} while (0)

/* 按指定 key 解析字符串 */
#define JSON_PARSE_STRING_NAME(json, member, name, size) do { \
    cJSON *_v = JSON_GET((json), (name)); \
    if (cJSON_IsString(_v) && _v->valuestring != NULL && (size) > 0) { \
        strncpy((member), _v->valuestring, (size) - 1); \
        (member)[(size) - 1] = '\0'; \
    } \
} while (0)

/* 按指定 key 写入 int */
#define JSON_ADD_INT_NAME(json, member, name) \
    cJSON_AddNumberToObject((json), (name), (double)(member))

/* 按指定 key 写入 uint */
#define JSON_ADD_UINT_NAME(json, member, name) \
    cJSON_AddNumberToObject((json), (name), (double)(member))

/* 按指定 key 写入 int64 */
#define JSON_ADD_INT64_NAME(json, member, name) \
    cJSON_AddNumberToObject((json), (name), (double)(member))

/* 按指定 key 写入 float */
#define JSON_ADD_FLOAT_NAME(json, member, name) \
    cJSON_AddNumberToObject((json), (name), (double)(member))

/* 按指定 key 写入 double */
#define JSON_ADD_DOUBLE_NAME(json, member, name) \
    cJSON_AddNumberToObject((json), (name), (double)(member))

/* 按指定 key 写入 bool */
#define JSON_ADD_BOOL_NAME(json, member, name) \
    cJSON_AddBoolToObject((json), (name), (member))

/* 按指定 key 写入字符串 */
#define JSON_ADD_STRING_NAME(json, member, name) \
    cJSON_AddStringToObject((json), (name), (member))

/* ============================================================
 * 带默认值的解析：字段缺失或类型不符时使用 def
 * ============================================================ */

/* int，缺省 def */
#define JSON_PARSE_INT_DEFAULT(json, member, def) do { \
    cJSON *_v = JSON_GET((json), #member); \
    (member) = cJSON_IsNumber(_v) ? _v->valueint : (def); \
} while (0)

/* uint，缺省 def */
#define JSON_PARSE_UINT_DEFAULT(json, member, def) do { \
    cJSON *_v = JSON_GET((json), #member); \
    (member) = (cJSON_IsNumber(_v) && _v->valuedouble >= 0) \
                   ? (unsigned int)_v->valuedouble : (def); \
} while (0)

/* float，缺省 def */
#define JSON_PARSE_FLOAT_DEFAULT(json, member, def) do { \
    cJSON *_v = JSON_GET((json), #member); \
    (member) = cJSON_IsNumber(_v) ? (float)_v->valuedouble : (def); \
} while (0)

/* double，缺省 def */
#define JSON_PARSE_DOUBLE_DEFAULT(json, member, def) do { \
    cJSON *_v = JSON_GET((json), #member); \
    (member) = cJSON_IsNumber(_v) ? _v->valuedouble : (def); \
} while (0)

/* bool，缺省 def */
#define JSON_PARSE_BOOL_DEFAULT(json, member, def) do { \
    cJSON *_v = JSON_GET((json), #member); \
    (member) = cJSON_IsBool(_v) ? cJSON_IsTrue(_v) : (def); \
} while (0)

/* 字符串，缺省 def；size 必须大于 0 */
#define JSON_PARSE_STRING_DEFAULT(json, member, size, def) do { \
    cJSON *_v = JSON_GET((json), #member); \
    const char *_s = cJSON_IsString(_v) ? _v->valuestring : (def); \
    if (_s != NULL && (size) > 0) { \
        strncpy((member), _s, (size) - 1); \
        (member)[(size) - 1] = '\0'; \
    } \
} while (0)

/* ============================================================
 * JSON 文本与对象生命周期
 * ============================================================ */

/* 删除 cJSON 节点树 */
#define JSON_DELETE(json) \
    cJSON_Delete(json)

/* 序列化为未格式化的紧凑 JSON 字符串（需 JSON_FREE_STRING 释放） */
#define JSON_PRINT(json) \
    cJSON_PrintUnformatted(json)

/* 从文本解析 JSON（失败返回 NULL） */
#define JSON_PARSE_TEXT(text) \
    cJSON_Parse(text)

/* 释放 JSON_PRINT 返回的字符串 */
#define JSON_FREE_STRING(text) \
    cJSON_free(text)

/* ============================================================
 * 对象/数组创建与挂载
 * ============================================================ */

/* 创建空对象 */
#define JSON_CREATE() \
    cJSON_CreateObject()

/* 创建空数组 */
#define JSON_CREATE_ARRAY() \
    cJSON_CreateArray()

/* 把已有节点挂到对象的指定 key 下 */
#define JSON_ADD_OBJECT(json, key, object) \
    cJSON_AddItemToObject((json), (key), (object))

/* 把已有节点追加到数组 */
#define JSON_ADD_ITEM_ARRAY(array, item) \
    cJSON_AddItemToArray((array), (item))

/* 从对象中摘除并返回指定 key 的节点（不释放） */
#define JSON_DETACH(json, key) \
    cJSON_DetachItemFromObjectCaseSensitive((json), (key))

#ifdef __cplusplus
}
#endif

#endif /* JSON_H */
