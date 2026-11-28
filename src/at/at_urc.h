#ifndef AT_URC_H
#define AT_URC_H

/*
 * URC（非请求上报）注册与分发。
 * 以行前缀匹配，与普通 AT 命令事务相互独立。
 *
 * 模块说明：
 *   - 注册表为固定容量（实现中为 8），先注册者优先匹配；
 *   - 只保存前缀指针不拷贝，前缀字符串需长期有效；
 *   - 回调在分发上下文（通常为 at_process）中同步执行。
 * 使用约束：
 *   - 前缀应按「最长优先」顺序注册，避免短前缀抢占长前缀的行；
 *   - 分发是行首匹配，行中间出现的相同文本不会触发 URC。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * URC 回调。
 * 参数：line 完整行内容；len 行长度；user 注册时传入的用户指针。
 */
typedef void (*at_urc_fn)(const char *line, uint32_t len, void *user);

/*
 * 注册 URC。
 * 参数：prefix 行首前缀（非 NULL）；callback 回调（非 NULL）；user 用户指针。
 * 返回：0 成功；参数非法或表已满返回 -1。
 */
int at_urc_register(const char *prefix, at_urc_fn callback, void *user);

/*
 * 注销 URC。
 * 参数：prefix 已注册的前缀。
 * 返回：0 成功；参数非法或未找到返回 -1。
 */
int at_urc_unregister(const char *prefix);

/* 若 line 匹配某 URC 前缀则调用其回调，返回 0；否则 -1 */
int at_urc_dispatch(const char *line, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* AT_URC_H */