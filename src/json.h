/**

JSON接口重定义，方便解析和生成，示例如下

#include "json.h"

typedef struct {
    int id;
    char name[32];
    bool enabled;
    float temperature;
    float values[4];
} device_t;

void test(void)
{
    const char *text =
        "{\"id\":1001,\"name\":\"sensor01\","
        "\"enabled\":true,\"temperature\":25.5,"
        "\"values\":[1.1,2.2,3.3,4.4]}";

    device_t dev = {0};

    // JSON -> struct
    cJSON *root = JSON_PARSE_TEXT(text);

    if (root) {
        JSON_PARSE_INT(root, dev.id);
        JSON_PARSE_STRING(root, dev.name, sizeof(dev.name));
        JSON_PARSE_BOOL(root, dev.enabled);
        JSON_PARSE_FLOAT(root, dev.temperature);
        JSON_PARSE_ARRAY_FLOAT(root, dev.values, 4);

        JSON_DELETE(root);
    }

    // struct -> JSON
    root = JSON_CREATE();

    if (root) {
        JSON_ADD_INT(root, dev.id);
        JSON_ADD_STRING(root, dev.name);
        JSON_ADD_BOOL(root, dev.enabled);
        JSON_ADD_FLOAT(root, dev.temperature);
        JSON_ADD_ARRAY_FLOAT(root, dev.values, 4);

        char *output = JSON_PRINT(root);

        if (output) {
            // 使用 output
            JSON_FREE_STRING(output);
        }

        JSON_DELETE(root);
    }
}

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
 * ============================================================ */

#define JSON_OK                 0
#define JSON_ERR_NULL          -1
#define JSON_ERR_TYPE          -2
#define JSON_ERR_MEMORY        -3
#define JSON_ERR_RANGE         -4
#define JSON_ERR_NOT_FOUND     -5
#define JSON_ERR_FORMAT        -6

/* ============================================================
 * 基础 JSON 类型判断
 * ============================================================ */

#define JSON_IS_NULL(v) \
    ((v) == NULL || cJSON_IsNull(v))

#define JSON_IS_BOOL(v) \
    (cJSON_IsTrue(v) || cJSON_IsFalse(v))

#define JSON_IS_NUMBER(v) \
    (cJSON_IsNumber(v))

#define JSON_IS_STRING(v) \
    (cJSON_IsString(v))

#define JSON_IS_ARRAY(v) \
    (cJSON_IsArray(v))

#define JSON_IS_OBJECT(v) \
    (cJSON_IsObject(v))

/* ============================================================
 * JSON 字段获取
 * ============================================================ */

#define JSON_GET(json, key) \
    cJSON_GetObjectItemCaseSensitive((json), (key))

#define JSON_GET_INT(json, key, value) do { \
    cJSON *_v = JSON_GET(json, key); \
    if (cJSON_IsNumber(_v)) { \
        (value) = _v->valueint; \
    } \
} while (0)

#define JSON_GET_UINT(json, key, value) do { \
    cJSON *_v = JSON_GET(json, key); \
    if (cJSON_IsNumber(_v) && _v->valuedouble >= 0) { \
        (value) = (unsigned int)_v->valuedouble; \
    } \
} while (0)

#define JSON_GET_INT64(json, key, value) do { \
    cJSON *_v = JSON_GET(json, key); \
    if (cJSON_IsNumber(_v)) { \
        (value) = (int64_t)_v->valuedouble; \
    } \
} while (0)

#define JSON_GET_FLOAT(json, key, value) do { \
    cJSON *_v = JSON_GET(json, key); \
    if (cJSON_IsNumber(_v)) { \
        (value) = (float)_v->valuedouble; \
    } \
} while (0)

#define JSON_GET_DOUBLE(json, key, value) do { \
    cJSON *_v = JSON_GET(json, key); \
    if (cJSON_IsNumber(_v)) { \
        (value) = _v->valuedouble; \
    } \
} while (0)

#define JSON_GET_BOOL(json, key, value) do { \
    cJSON *_v = JSON_GET(json, key); \
    if (cJSON_IsBool(_v)) { \
        (value) = cJSON_IsTrue(_v); \
    } \
} while (0)

#define JSON_GET_STRING(json, key, value, size) do { \
    cJSON *_v = JSON_GET(json, key); \
    if (cJSON_IsString(_v) && _v->valuestring) { \
        strncpy((value), _v->valuestring, (size) - 1); \
        (value)[(size) - 1] = '\0'; \
    } \
} while (0)

/* ============================================================
 * JSON 字段设置
 * ============================================================ */

#define JSON_SET_INT(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

#define JSON_SET_UINT(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

#define JSON_SET_INT64(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

#define JSON_SET_FLOAT(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

#define JSON_SET_DOUBLE(json, key, value) \
    cJSON_AddNumberToObject((json), (key), (double)(value))

#define JSON_SET_BOOL(json, key, value) \
    cJSON_AddBoolToObject((json), (key), (value))

#define JSON_SET_STRING(json, key, value) \
    cJSON_AddStringToObject((json), (key), (value))

/* ============================================================
 * JSON -> struct 字段解析
 *
 * 使用方式：
 * JSON_PARSE_INT(json, id);
 * JSON_PARSE_STRING(json, name, sizeof(name));
 * ============================================================ */

#define JSON_PARSE_INT(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v)) { \
        (member) = _v->valueint; \
    } \
} while (0)

#define JSON_PARSE_UINT(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v) && _v->valuedouble >= 0) { \
        (member) = (unsigned int)_v->valuedouble; \
    } \
} while (0)

#define JSON_PARSE_INT8(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= INT8_MIN && \
        _v->valuedouble <= INT8_MAX) { \
        (member) = (int8_t)_v->valueint; \
    } \
} while (0)

#define JSON_PARSE_UINT8(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= 0 && \
        _v->valuedouble <= UINT8_MAX) { \
        (member) = (uint8_t)_v->valueint; \
    } \
} while (0)

#define JSON_PARSE_INT16(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= INT16_MIN && \
        _v->valuedouble <= INT16_MAX) { \
        (member) = (int16_t)_v->valueint; \
    } \
} while (0)

#define JSON_PARSE_UINT16(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= 0 && \
        _v->valuedouble <= UINT16_MAX) { \
        (member) = (uint16_t)_v->valueint; \
    } \
} while (0)

#define JSON_PARSE_INT32(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= INT32_MIN && \
        _v->valuedouble <= INT32_MAX) { \
        (member) = (int32_t)_v->valuedouble; \
    } \
} while (0)

#define JSON_PARSE_UINT32(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= 0 && \
        _v->valuedouble <= UINT32_MAX) { \
        (member) = (uint32_t)_v->valuedouble; \
    } \
} while (0)

#define JSON_PARSE_INT64(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= -9223372036854775808.0 && \
        _v->valuedouble < 9223372036854775808.0) { \
        (member) = (int64_t)_v->valuedouble; \
    } \
} while (0)

#define JSON_PARSE_UINT64(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v) && \
        _v->valuedouble >= 0 && \
        _v->valuedouble < 18446744073709551616.0) { \
        (member) = (uint64_t)_v->valuedouble; \
    } \
} while (0)

#define JSON_PARSE_FLOAT(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v)) { \
        (member) = (float)_v->valuedouble; \
    } \
} while (0)

#define JSON_PARSE_DOUBLE(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsNumber(_v)) { \
        (member) = _v->valuedouble; \
    } \
} while (0)

#define JSON_PARSE_BOOL(json, member) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsBool(_v)) { \
        (member) = cJSON_IsTrue(_v); \
    } \
} while (0)

#define JSON_PARSE_STRING(json, member, size) do { \
    cJSON *_v = JSON_GET(json, #member); \
    if (cJSON_IsString(_v) && _v->valuestring) { \
        strncpy((member), _v->valuestring, (size) - 1); \
        (member)[(size) - 1] = '\0'; \
    } \
} while (0)

/* ============================================================
 * struct -> JSON 字段序列化
 * ============================================================ */

#define JSON_ADD_INT(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

#define JSON_ADD_UINT(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

#define JSON_ADD_INT64(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

#define JSON_ADD_UINT64(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

#define JSON_ADD_FLOAT(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

#define JSON_ADD_DOUBLE(json, member) \
    cJSON_AddNumberToObject((json), #member, (double)(member))

#define JSON_ADD_BOOL(json, member) \
    cJSON_AddBoolToObject((json), #member, (member))

#define JSON_ADD_STRING(json, member) \
    cJSON_AddStringToObject((json), #member, (member))

/* ============================================================
 * 数组解析
 * ============================================================ */

#define JSON_PARSE_ARRAY_INT(json, member, count) do { \
    cJSON *_arr = JSON_GET(json, #member); \
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

#define JSON_PARSE_ARRAY_FLOAT(json, member, count) do { \
    cJSON *_arr = JSON_GET(json, #member); \
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

#define JSON_PARSE_ARRAY_BOOL(json, member, count) do { \
    cJSON *_arr = JSON_GET(json, #member); \
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
 * 数组序列化
 * ============================================================ */

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

/* ============================================================
 * 字段别名
 * ============================================================ */

#define JSON_PARSE_INT_NAME(json, member, name) do { \
    cJSON *_v = JSON_GET(json, name); \
    if (cJSON_IsNumber(_v)) { \
        (member) = _v->valueint; \
    } \
} while (0)

#define JSON_PARSE_STRING_NAME(json, member, name, size) do { \
    cJSON *_v = JSON_GET(json, name); \
    if (cJSON_IsString(_v) && _v->valuestring) { \
        strncpy((member), _v->valuestring, (size) - 1); \
        (member)[(size) - 1] = '\0'; \
    } \
} while (0)

#define JSON_ADD_INT_NAME(json, member, name) \
    cJSON_AddNumberToObject((json), name, (double)(member))

#define JSON_ADD_STRING_NAME(json, member, name) \
    cJSON_AddStringToObject((json), name, (member))

/* ============================================================
 * 默认值
 * ============================================================ */

#define JSON_PARSE_INT_DEFAULT(json, member, def) do { \
    cJSON *_v = JSON_GET(json, #member); \
    (member) = cJSON_IsNumber(_v) ? _v->valueint : (def); \
} while (0)

#define JSON_PARSE_BOOL_DEFAULT(json, member, def) do { \
    cJSON *_v = JSON_GET(json, #member); \
    (member) = cJSON_IsBool(_v) ? cJSON_IsTrue(_v) : (def); \
} while (0)

#define JSON_PARSE_STRING_DEFAULT(json, member, size, def) do { \
    cJSON *_v = JSON_GET(json, #member); \
    const char *_s = cJSON_IsString(_v) ? _v->valuestring : (def); \
    if (_s) { \
        strncpy((member), _s, (size) - 1); \
        (member)[(size) - 1] = '\0'; \
    } \
} while (0)

/* ============================================================
 * JSON 字符串处理
 * ============================================================ */

#define JSON_DELETE(json) \
    cJSON_Delete(json)

#define JSON_PRINT(json) \
    cJSON_PrintUnformatted(json)

#define JSON_PARSE_TEXT(text) \
    cJSON_Parse(text)

#define JSON_FREE_STRING(text) \
    cJSON_free(text)

/* ============================================================
 * JSON 对象创建辅助
 * ============================================================ */

#define JSON_CREATE() \
    cJSON_CreateObject()

#define JSON_CREATE_ARRAY() \
    cJSON_CreateArray()

#define JSON_ADD_OBJECT(json, key, object) \
    cJSON_AddItemToObject((json), key, object)

/* ============================================================
 * C++ 兼容
 * ============================================================ */

#ifdef __cplusplus
}
#endif

#endif /* JSON_H */
