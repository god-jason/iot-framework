/*
 * config.c - 基于 kvfs 的配置存储（单例，与平台/芯片无关）
 *
 * 配置结构体以原始内存形式作为 KV 的一条 value：
 *   save   -> kvfs_put：追加记录，写头与数据后回读校验，最后写 Commit；
 *   load   -> kvfs_get：取该键最新值；
 *   format -> kvfs_del：追加墓碑记录，旧记录由压缩回收。
 *
 * 上下文只保留一份（config_init 绑定），双区滚动、CRC32、掉电恢复、
 * 空间压缩全部由 components/kvfs 负责。
 */

#include "config.h"

#include <stddef.h>

static struct {
    kvfs_t  *kv;
    uint32_t key;
    uint32_t max_size;
    uint16_t version;
    uint8_t  ready;
} s_cfg;

static int config_ready(void)
{
    return (s_cfg.ready != 0U && s_cfg.kv != NULL) ? 1 : 0;
}

int config_init(kvfs_t *kv, uint32_t key, uint16_t version, uint32_t max_size)
{
    if (kv == NULL || max_size == 0U) {
        return CONFIG_ERR_PARAM;
    }

    s_cfg.kv = kv;
    s_cfg.key = key;
    s_cfg.max_size = max_size;
    s_cfg.version = version;
    s_cfg.ready = 1U;

    return config_check();
}

int config_check(void)
{
    int rc;

    if (config_ready() == 0) {
        return CONFIG_ERR_PARAM;
    }

    rc = kvfs_exists(s_cfg.kv, s_cfg.key);
    if (rc == 1) {
        return CONFIG_OK;
    }
    if (rc == 0) {
        return CONFIG_NONE;
    }
    return CONFIG_ERR_IO;
}

int config_load(void *data, uint32_t size)
{
    uint32_t len = 0U;
    int rc;

    if (config_ready() == 0 || data == NULL || size == 0U) {
        return CONFIG_ERR_PARAM;
    }
    if (size > s_cfg.max_size) {
        return CONFIG_ERR_SIZE;
    }

    rc = kvfs_get(s_cfg.kv, s_cfg.key, data, size, &len);
    if (rc == KVFS_OK) {
        return (len == size) ? CONFIG_OK : CONFIG_ERR_SIZE;
    }
    if (rc == KVFS_ERR_NOENT) {
        return CONFIG_NONE;
    }
    return (rc == KVFS_ERR_NOMEM) ? CONFIG_ERR_SIZE : CONFIG_ERR_IO;
}

int config_load_raw(void *buf, uint32_t cap, uint32_t *len, uint16_t *version)
{
    uint32_t n = 0U;
    int rc;

    if (config_ready() == 0 || buf == NULL || cap == 0U) {
        return CONFIG_ERR_PARAM;
    }

    rc = kvfs_get(s_cfg.kv, s_cfg.key, buf, cap, &n);
    if (rc == KVFS_OK) {
        if (len != NULL) {
            *len = n;
        }
        if (version != NULL) {
            *version = s_cfg.version;
        }
        return CONFIG_OK;
    }
    if (rc == KVFS_ERR_NOENT) {
        return CONFIG_NONE;
    }
    if (len != NULL) {
        *len = n;
    }
    return (rc == KVFS_ERR_NOMEM) ? CONFIG_ERR_SIZE : CONFIG_ERR_IO;
}

int config_save(const void *data, uint32_t size)
{
    int rc;

    if (config_ready() == 0 || data == NULL || size == 0U) {
        return CONFIG_ERR_PARAM;
    }
    if (size > s_cfg.max_size) {
        return CONFIG_ERR_SIZE;
    }

    rc = kvfs_put(s_cfg.kv, s_cfg.key, data, size);
    if (rc == KVFS_OK) {
        return CONFIG_OK;
    }
    return (rc == KVFS_ERR_INVAL) ? CONFIG_ERR_SIZE : CONFIG_ERR_IO;
}

int config_format(void)
{
    int rc;

    if (config_ready() == 0) {
        return CONFIG_ERR_PARAM;
    }

    rc = kvfs_del(s_cfg.kv, s_cfg.key);
    return (rc == KVFS_OK || rc == KVFS_ERR_NOENT) ? CONFIG_OK : CONFIG_ERR_IO;
}
