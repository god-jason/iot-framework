/*
 * var_json.h - var_t 与 JSON 文本互转（不依赖 cJSON）
 *
 * 本模块自带精简 JSON 解析器/打印器：
 *   - var_to_json()：按 JSON 语法输出紧凑文本（malloc 分配）；
 *   - var_from_json()：解析标量 JSON，字符串复制到调用方缓冲。
 *
 * 序列化覆盖全部类型：
 *   NONE→null，BOOL→true/false，整数→number，FLOAT32/FLOAT64→number，
 *   STRING→string，ARRAY→array（约定 vec 元素为 var_t，
 *   elem_size == sizeof(var_t)），OBJECT→object（约定 map 的 value 为 var_t*）。
 *   NaN/Inf 等非有限浮点输出为 null。
 *
 * 反序列化只支持标量（null/bool/number/string）：
 *   数组/对象需要调用方自行分配容器，本层不创建 vec_t/map_t。
 *   支持 \" \\ \/ \b \f \n \r \t \uXXXX（含代理对）转义，
 *   解析后要求整个输入只包含一个 JSON 值（可带首尾空白）。
 *
 * 内存约定：
 *   - var_to_json() 返回的文本由 ros_mem 分配，必须用 var_free_json()
 *     释放；
 *   - var_from_json() 解析出的字符串落在调用方 strbuf，返回后无需
 *     依赖任何中间状态，可安全长期使用。
 */

#ifndef VAR_JSON_H
#define VAR_JSON_H

#include "var.h"

#ifdef __cplusplus
extern "C" {
#endif

/* var -> 紧凑 JSON 文本；返回串用 var_free_json 释放，失败返回 NULL */
char *var_to_json(const var_t *v);

/*
 * JSON 文本 -> var（仅标量）。
 * 数字：无小数/指数且落在 int32 范围按 VAR_INT32，否则按 VAR_FLOAT32。
 * 字符串：复制到 strbuf（截断到 strcap-1）；strbuf 为 NULL 时
 * 只接受非字符串类型，否则返回 false（out 保持原值）。
 */
bool var_from_json(const char *text, var_t *out, char *strbuf, uint32_t strcap);

/*
 * 完整 JSON 解析：支持对象与数组。
 *   - 整数按 int32 -> int64 -> uint64 -> float32 依次降级；
 *   - 对象/数组通过 ros_mem 分配，所有权归 *out，使用后调用 var_free()；
 *   - 所有字符串（含对象键）顺序写入 strbuf，解析结果中的 VAR_STRING
 *     指向该区域，因此 strbuf 的生命周期必须覆盖 var 的使用；
 *     空间不足时解析失败（不静默截断）。
 * 返回 true 成功；false 时 *out 置为 VAR_NONE，已分配的容器全部释放。
 */
bool var_from_json_ex(const char *text, var_t *out, char *strbuf,
                      uint32_t strcap);

/* 释放 var_to_json() 返回的字符串 */
void var_free_json(char *text);

#ifdef __cplusplus
}
#endif

#endif /* VAR_JSON_H */
