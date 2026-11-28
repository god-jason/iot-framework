#ifndef PARAMS_H
#define PARAMS_H

/*
 * params.h - 运行参数表（默认值 / 范围校验 / 序列化）
 *
 * 用于保存机器人配置（轮径、PID 增益、限位等）：
 *   - 每个参数带默认值与允许范围，越界写入会被拒绝；
 *   - 支持文本序列化 "name=value\n"，便于通过串口/存储保存与恢复；
 *   - 固定容量静态数组，无动态内存。
 *
 * 文本格式：每行 "name=value"，忽略空行与以 '#' 开头的注释行。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PARAM_NAME_MAX   24U
#define PARAM_STORE_MAX  32U

typedef struct {
    char name[PARAM_NAME_MAX];   /* 参数名（以 '\0' 结尾） */
    float value;                 /* 当前值 */
    float def;                   /* 默认值 */
    float min;                   /* 允许下限 */
    float max;                   /* 允许上限 */
    bool used;                   /* 槽位是否被占用 */
} param_entry_t;

typedef struct {
    param_entry_t items[PARAM_STORE_MAX];
    uint16_t count;              /* 已注册参数个数 */
} param_store_t;

/* 初始化参数表 */
void params_init(param_store_t *store);

/*
 * 注册参数：要求 min <= def <= max，名称非空且不重复。
 * 返回：0 成功；-1 参数非法、重名或表已满。
 */
int params_add(param_store_t *store, const char *name,
               float def, float min, float max);

/*
 * 设置参数值：超出 [min, max] 会被拒绝。
 * 返回：0 成功；-1 参数不存在或越界。
 */
int params_set(param_store_t *store, const char *name, float value);

/* 读取参数值 */
int params_get(const param_store_t *store, const char *name, float *value);

/* 恢复单个参数为默认值 */
int params_reset(param_store_t *store, const char *name);

/* 全部恢复默认值 */
void params_reset_all(param_store_t *store);

/*
 * 序列化为 "name=value\n" 文本。
 * 返回：写入长度（不含结尾 '\0'）；空间不足或参数非法返回 -1。
 */
int params_serialize(const param_store_t *store, char *buf, uint32_t size);

/*
 * 从文本恢复参数；未注册的名称会被忽略。
 * 返回：成功应用的参数个数；文本非法或为空返回 -1。
 */
int params_deserialize(param_store_t *store, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* PARAMS_H */
