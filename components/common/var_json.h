/*
 * var_json.h - var_t 与 JSON 的互转（基于 vendor/cjson）
 *
 * 序列化覆盖全部类型：
 *   NONE→null，BOOL→true/false，INT/FLOAT→number，STRING→string，
 *   ARRAY→array（约定 vec 元素为 var_t，elem_size == sizeof(var_t)），
 *   OBJECT→object（约定 map 的 value 为 var_t*）。
 *
 * 反序列化只支持标量（null/bool/number/string）：
 *   数组/对象需要调用方自行分配容器，本层不创建 vec_t/map_t。
 *
 * 引用语义：
 *   - var_from_cjson() 解析出的 STRING 指向 cJSON 节点，节点树必须保持存活；
 *   - var_from_json() 会把字符串复制到调用方缓冲，返回后即安全。
 *
 * 本模块依赖 cJSON 的内存分配（默认 malloc/free）：
 *   var_to_json() 返回的文本必须用 var_free_json() 释放。
 */

#ifndef VAR_JSON_H
#define VAR_JSON_H

#include "var.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;

/*
 * var -> cJSON 节点（调用者负责 cJSON_Delete）。
 * 失败（参数非法、数组元素类型不符、嵌套过深等）返回 NULL。
 */
struct cJSON *var_to_cjson(const var_t *v);

/*
 * cJSON 节点 -> var（仅标量）。
 * 数字：能精确表示为 int32 的按 VAR_INT，否则按 VAR_FLOAT。
 * 字符串：借用节点的 valuestring，节点树释放后失效。
 * 数组/对象或参数非法返回 false（out 保持原值）。
 */
bool var_from_cjson(const struct cJSON *json, var_t *out);

/* var -> 紧凑 JSON 文本；返回串用 var_free_json 释放，失败返回 NULL */
char *var_to_json(const var_t *v);

/*
 * JSON 文本 -> var（仅标量）。
 * 字符串会复制到 strbuf（截断到 strcap-1）；strbuf 为 NULL 时
 * 只接受非字符串类型，否则返回 false。
 */
bool var_from_json(const char *text, var_t *out, char *strbuf, uint32_t strcap);

/* 释放 var_to_json() 返回的字符串 */
void var_free_json(char *text);

#ifdef __cplusplus
}
#endif

#endif /* VAR_JSON_H */
