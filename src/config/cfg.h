/*
 * cfg.h - 配置结构体 <-> var_t 的 X 宏映射
 *
 * 纯 C 无法从变量地址推导结构体类型，因此字段表由一个 X 宏描述，
 * 由 CFG_DEFINE 一次生成结构体与四个转换函数：
 *   - CFG_TO_VAR(name, &s, &v)：结构体 -> var_t 对象；
 *   - CFG_FROM_VAR(name, &s, &v)：var_t 对象 -> 结构体，缺失字段填默认值；
 *   - CFG_TO_JSON(name, &s)：结构体 -> JSON 文本（var_free_json 释放）；
 *   - CFG_FROM_JSON(name, &s, text, buf, cap)：JSON 文本 -> 结构体，
 *     字符串写入 buf，缺失字段填默认值。
 *
 * 用法：
 *
 *   #define DEV_CFG(X) \
 *       X(int32,  id, 0) \
 *       X(bool,   enabled, true) \
 *       X(float32, temperature, 25.0f) \
 *       X(string, name, "device", 32)
 *
 *   CFG_DEFINE(dev, DEV_CFG)
 *
 *   dev n = {0};
 *   var_t v;
 *   char buf[128];
 *
 *   CFG_FROM_VAR(dev, &n, &v);                    // var -> struct（带默认值）
 *   CFG_TO_VAR(dev, &n, &v);                      // struct -> var
 *   char *json = CFG_TO_JSON(dev, &n);            // struct -> JSON
 *   CFG_FROM_JSON(dev, &n, json, buf, sizeof(buf)); // JSON -> struct
 *
 * 类型 token 与实际字段类型：
 *   bool    -> bool
 *   uint8   -> uint8_t        int8   -> int8_t
 *   uint16  -> uint16_t       int16  -> int16_t
 *   uint32  -> uint32_t       int32  -> int32_t
 *   uint64  -> uint64_t       int64  -> int64_t
 *   float32 -> float          float64 -> double
 *   string  -> char[N]，N 的确定方式：
 *              X(string, 成员, "默认值")        // N = sizeof(默认值)
 *              X(string, 成员, "默认值", 容量)  // N = 容量
 *
 * 说明：
 *   - 条目格式为 X(类型, 成员, 默认值)；string 可再跟一个容量参数；
 *   - 容量省略时以默认值字面量的长度（含 '\0'）作为数组大小；
 *   - CFG_TO_VAR 会先 var_object_new(v)，v 原值不释放，调用前请确保 v 为空；
 *   - CFG_FROM_VAR 使用 var_to_* 转换，字段缺失/越界/类型不符时写入默认值；
 *   - CFG_FROM_JSON 解析失败时返回 false，但结构体仍会被填满默认值；
 *   - string 字段是定长数组，转换时按容量复制并截断，不借用外部内存；
 *     CFG_FROM_JSON 的 buf 只是解析临时区，调用返回后即可复用。
 *   - 不支持嵌套结构体与可变长/多字符串数组，需要时可在 X 宏外单独处理；
 *   - name##_to_json / name##_from_json 以 void* 接收结构体指针，
 *     可直接注册到 config 的类型表（见 config.h 的 CFG_REGISTER）。
 */

#ifndef CFG_H
#define CFG_H

#include "var.h"
#include "var_json.h"
#include "strings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 字段声明 ---------------- */

#define CFG_FIELD_DECL(type, member, ...) CFG_DECL_##type(member, __VA_ARGS__)

#define CFG_DECL_bool(m, d)      bool m;
#define CFG_DECL_int8(m, d)      int8_t m;
#define CFG_DECL_uint8(m, d)     uint8_t m;
#define CFG_DECL_int16(m, d)     int16_t m;
#define CFG_DECL_uint16(m, d)    uint16_t m;
#define CFG_DECL_int32(m, d)     int32_t m;
#define CFG_DECL_uint32(m, d)    uint32_t m;
#define CFG_DECL_int64(m, d)     int64_t m;
#define CFG_DECL_uint64(m, d)    uint64_t m;
#define CFG_DECL_float32(m, d)   float m;
#define CFG_DECL_float64(m, d)   double m;
#define CFG_DECL_string(m, ...)  char m[CFG_STR_CAP(__VA_ARGS__)];

/* string 数组大小：给了容量用容量，否则用默认字面量的 sizeof */
#define CFG_STR_CAP(...) \
    CFG_STR_CAP_PICK(__VA_ARGS__, CFG_STR_CAP_2, CFG_STR_CAP_1)(__VA_ARGS__)
#define CFG_STR_CAP_PICK(_1, _2, NAME, ...) NAME
#define CFG_STR_CAP_1(d)      sizeof(d)
#define CFG_STR_CAP_2(d, cap) (cap)

/* ---------------- struct -> var ---------------- */

#define CFG_FIELD_SET(type, member, ...) CFG_SET_##type(v, s, member);

#define CFG_SET_bool(v, s, m) \
    (void)var_object_put_bool((v), #m, (s)->m)
#define CFG_SET_int8(v, s, m) \
    (void)var_object_put_int8((v), #m, (s)->m)
#define CFG_SET_uint8(v, s, m) \
    (void)var_object_put_uint8((v), #m, (s)->m)
#define CFG_SET_int16(v, s, m) \
    (void)var_object_put_int16((v), #m, (s)->m)
#define CFG_SET_uint16(v, s, m) \
    (void)var_object_put_uint16((v), #m, (s)->m)
#define CFG_SET_int32(v, s, m) \
    (void)var_object_put_int32((v), #m, (s)->m)
#define CFG_SET_uint32(v, s, m) \
    (void)var_object_put_uint32((v), #m, (s)->m)
#define CFG_SET_int64(v, s, m) \
    (void)var_object_put_int64((v), #m, (s)->m)
#define CFG_SET_uint64(v, s, m) \
    (void)var_object_put_uint64((v), #m, (s)->m)
#define CFG_SET_float32(v, s, m) \
    (void)var_object_put_float32((v), #m, (s)->m)
#define CFG_SET_float64(v, s, m) \
    (void)var_object_put_float64((v), #m, (s)->m)
#define CFG_SET_string(v, s, m) \
    (void)var_object_put_str((v), #m, (s)->m)

/* ---------------- var -> struct（带默认值） ---------------- */

#define CFG_FIELD_GET(type, member, ...) CFG_GET_##type(v, s, member, __VA_ARGS__);

#define CFG_GET_bool(v, s, m, d) do { \
    if (!var_to_bool(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_int8(v, s, m, d) do { \
    if (!var_to_int8(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_uint8(v, s, m, d) do { \
    if (!var_to_uint8(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_int16(v, s, m, d) do { \
    if (!var_to_int16(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_uint16(v, s, m, d) do { \
    if (!var_to_uint16(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_int32(v, s, m, d) do { \
    if (!var_to_int32(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_uint32(v, s, m, d) do { \
    if (!var_to_uint32(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_int64(v, s, m, d) do { \
    if (!var_to_int64(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_uint64(v, s, m, d) do { \
    if (!var_to_uint64(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_float32(v, s, m, d) do { \
    if (!var_to_float32(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_float64(v, s, m, d) do { \
    if (!var_to_float64(var_object_get((v), #m), &(s)->m)) { \
        (s)->m = (d); \
    } \
} while (0)

#define CFG_GET_string(v, s, m, d, ...) do { \
    const char *_cfg_s = var_get_string(var_object_get((v), #m)); \
    (void)str_copy((s)->m, (uint32_t)sizeof((s)->m), \
                   (_cfg_s != NULL) ? _cfg_s : (d)); \
} while (0)

/* ---------------- 生成结构体与转换函数 ---------------- */

#define CFG_DEFINE(name, LIST) \
    typedef struct name { \
        LIST(CFG_FIELD_DECL) \
    } name; \
    \
    static inline void name##_to_var(const name *s, var_t *v) { \
        if (var_object_new(v) == NULL) { \
            return; \
        } \
        LIST(CFG_FIELD_SET) \
    } \
    \
    static inline void name##_from_var(const var_t *v, name *s) { \
        LIST(CFG_FIELD_GET) \
    } \
    \
    static inline char *name##_to_json(const void *p) { \
        const name *s = (const name *)p; \
        var_t v; \
        char *text; \
        v.type = VAR_NONE; \
        name##_to_var(s, &v); \
        if (v.type != VAR_OBJECT) { \
            return NULL; \
        } \
        text = var_to_json(&v); \
        var_free(&v); \
        return text; \
    } \
    \
    static inline bool name##_from_json(void *p, const char *text, \
                                        char *buf, uint32_t cap) { \
        name *s = (name *)p; \
        var_t v; \
        bool ok; \
        v.type = VAR_NONE; \
        ok = var_from_json_ex(text, &v, buf, cap); \
        name##_from_var(&v, s); \
        var_free(&v); \
        return ok; \
    }

/* ---------------- 调用入口 ---------------- */

#define CFG_TO_VAR(name, s, v) \
    name##_to_var((s), (v))

#define CFG_FROM_VAR(name, s, v) \
    name##_from_var((v), (s))

#define CFG_TO_JSON(name, s) \
    name##_to_json((s))

#define CFG_FROM_JSON(name, s, text, buf, cap) \
    name##_from_json((s), (text), (buf), (cap))

#ifdef __cplusplus
}
#endif

#endif /* CFG_H */
