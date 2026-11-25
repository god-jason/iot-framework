#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h - 基于 fkv 的配置存储（单例）
 * ============================================================================
 *
 * 进程内只保留一份配置存储上下文：config_init() 绑定 fkv 实例与键，
 * 之后的读写不再传 store 参数。
 *
 * 直接调用 components/fkv 的接口：
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

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
