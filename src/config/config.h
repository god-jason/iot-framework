#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h - 基于 fkv 的配置存储（单例）
 * ============================================================================
 *
 * 进程内只保留一份配置存储上下文：config_init() 绑定 fkv 实例与键，
 * 之后的读写不再传 store 参数。
 *
 * 直接调用 src/fkv 的接口：
 *   - save   = fkv_put（追加记录，Header/Payload 回读校验后最后写 Commit）
 *   - load   = fkv_get（取该键最新值）
 *   - format = fkv_del（追加墓碑记录，旧数据由压缩回收）
 *
 * 双区滚动、CRC32、掉电恢复与压缩全部由 fkv 负责；本层只做参数检查与
 * 错误码归一化，不直接访问 Flash，也不依赖具体芯片的地址与擦写原语。
 *
 * 用法：
 *   config_init(boot_kv(), KEY, 1U, sizeof(my_cfg));
 *   config_load(&my_cfg, sizeof(my_cfg));
 *   config_save(&my_cfg, sizeof(my_cfg));
 */

#include "fkv.h"
#include "cfg.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 返回码（NONE 表示尚无配置记录） */
#define CONFIG_OK           0
#define CONFIG_NONE         1
#define CONFIG_ERR_PARAM    (-1)
#define CONFIG_ERR_IO       (-2)
#define CONFIG_ERR_SIZE     (-3)

/*
 * 绑定 fkv 实例与配置键，并检查是否已有配置。
 * 返回 CONFIG_OK（已有配置）/ CONFIG_NONE（尚无配置）或错误码。
 */
int config_init(fkv_t *kv, uint32_t key, uint16_t version, uint32_t max_size);

/* 加载最新配置（长度必须与保存时一致） */
int config_load(void *data, uint32_t size);

/*
 * 按缓冲容量加载，并输出实际长度与版本号，
 * 供结构体升级时按 version/length 做迁移。
 */
int config_load_raw(void *buf, uint32_t cap, uint32_t *len, uint16_t *version);

/* 保存配置（追加一条 KV 记录） */
int config_save(const void *data, uint32_t size);

/* 重新检查配置是否存在：CONFIG_OK / CONFIG_NONE / 错误码 */
int config_check(void);

/* 删除配置记录（旧数据由 fkv 压缩回收） */
int config_format(void);

/* ============================ 配置类型注册 ============================ */

/*
 * 一个已注册的配置类型：
 *   id        KV 键，记录以该键存放在 config_init 绑定的 fkv 上；
 *   name      命令行使用的名称（同时是结构体类型名）；
 *   size      结构体字节数，读写长度必须完全一致；
 *   to_json   name##_to_json（结构体 -> JSON 文本，var_free_json 释放）；
 *   from_json name##_from_json（JSON -> 结构体，缺失字段填默认值）。
 */
typedef struct {
    uint32_t id;
    const char *name;
    uint32_t size;
    char *(*to_json)(const void *data);
    bool (*from_json)(void *data, const char *text, char *buf, uint32_t cap);
} config_item_t;

/*
 * 注册一个由 cfg.h 生成的配置类型（在函数内调用）：
 *
 *   CFG_DEFINE(my_cfg, MY_CFG)
 *   ...
 *   CFG_REGISTER(KEY_MY_CFG, my_cfg);
 *
 * 宏会生成静态的 config_item_t（保证生命周期），并登记到全局表；
 * 重复注册同一项返回 CONFIG_OK。注册后可用配置命令按 JSON 读写。
 */
#define CFG_REGISTER(id, name) \
    do { \
        static const config_item_t s_cfg_item_##name = { \
            (id), #name, (uint32_t)sizeof(name), \
            name##_to_json, name##_from_json \
        }; \
        (void)config_register(&s_cfg_item_##name); \
    } while (0)

/* 注册配置项：CONFIG_OK / CONFIG_ERR_PARAM（参数错、id/名称冲突）/ CONFIG_ERR_SIZE（表满） */
int config_register(const config_item_t *item);

/* 按名称查找；未注册返回 NULL */
const config_item_t *config_find(const char *name);

/* 已注册项数与按下标访问 */
uint32_t config_item_count(void);
const config_item_t *config_item_at(uint32_t index);

/*
 * 按 KV 键直接读写原始结构体（供多配置项使用，不改变单例绑定的键）：
 *   load：长度必须与 size 完全一致，否则返回 CONFIG_ERR_SIZE；
 *   save：追加一条记录；
 *   delete：删除该键（不存在也算成功）。
 */
int config_key_load(uint32_t key, void *data, uint32_t size);
int config_key_save(uint32_t key, const void *data, uint32_t size);
int config_key_delete(uint32_t key);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
