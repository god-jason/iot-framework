#ifndef STORAGE_KV_H
#define STORAGE_KV_H

/*
 * KV 键值存储（默认 RAM 后端，可扩展 Flash/持久化）。
 *
 * 模块说明：
 *   - 以「键 + 类型 + 原始字节值」的形式保存小型配置项；
 *   - 默认实现只在 RAM 中维护静态表，掉电即丢失；
 *   - 值以调用者提供的字节数和原始内存拷贝，不做类型转换。
 * 容量约束（实现固定值）：
 *   - 最多 32 个条目；键名最长 15 字符；值最长 32 字节；
 *   - 非线程安全，多任务场景需外部加锁或固定单上下文访问。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 值类型标记；get 时类型需与 set 时一致，否则读取失败 */
typedef enum {
    STORAGE_KV_INT,      /* 整型值（按原始字节存储） */
    STORAGE_KV_FLOAT,    /* 浮点值（按原始字节存储） */
    STORAGE_KV_STRING,   /* 字符串（通常含结尾 '\0'） */
    STORAGE_KV_BLOB      /* 任意二进制块 */
} storage_kv_type_t;

/*
 * 初始化 KV 表（清空所有条目）。
 * 返回：0。
 */
int storage_kv_init(void);

/*
 * 写入或更新键值。
 * 参数：key 键名（非 NULL，超长截断）；type 值类型；
 *       value 值数据（非 NULL）；len 值字节数（0..32）。
 * 返回：0 成功；参数非法、值超长或表已满返回 -1。
 */
int storage_kv_set(const char *key, storage_kv_type_t type,
               const void *value, uint32_t len);

/*
 * 读取键值。
 * 参数：key 键名；type 期望类型；value 输出缓冲（非 NULL）；
 *       size 输出缓冲容量；actual_size 可选，返回条目真实字节数。
 * 返回：0 成功；键不存在、类型不匹配或参数非法返回 -1。
 * 说明：size 不足时按 size 截断拷贝，可通过 actual_size 判断是否截断。
 */
int storage_kv_get(const char *key, storage_kv_type_t type,
               void *value, uint32_t size, uint32_t *actual_size);

/*
 * 删除单个键。
 * 参数：key 键名。
 * 返回：0 成功；键不存在或参数非法返回 -1。
 */
int storage_kv_delete(const char *key);

/*
 * 清空所有键值。
 * 返回：0。
 */
int storage_kv_clear(void);

/*
 * 提交/落盘（RAM 后端为空操作，预留持久化扩展）。
 * 返回：0。
 */
int storage_kv_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_KV_H */