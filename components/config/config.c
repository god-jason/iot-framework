/*
 * config.c - 基于 fkv 的配置存储（单例）
 *
 * 配置结构体以原始内存形式作为 KV 的一条 value：
 *   save   -> fkv_put：追加记录，写头与数据后回读校验，最后写 Commit；
 *   load   -> fkv_get：取该键最新值；
 *   format -> fkv_del：追加墓碑记录，旧记录由压缩回收。
 *
 * 上下文只保留一份（config_init 绑定），双区滚动、CRC32、掉电恢复、
 * 空间压缩全部由 components/fkv 负责；fkv 本身不加锁，并发保护由本层
 * 用一把 ROS 互斥量完成（调度器未启动时自动降级为无锁单线程访问）。
 */

#include "config.h"

#include "ros_mutex.h"
#include "vec.h"

#include <stddef.h>
#include <string.h>

/*
 * 已注册的配置类型表（CFG_REGISTER 登记，进程内单份）。
 * 元素类型为 const config_item_t *，按需扩容，数量不设上限。
 */
static vec_t s_items;
static bool s_items_ready;

/* 首次注册时初始化 vec；失败返回 -1 */
static int config_items_init(void)
{
    if (s_items_ready) {
        return 0;
    }
    if (vec_init(&s_items, sizeof(const config_item_t *), 0U) != 0) {
        return -1;
    }
    s_items_ready = true;
    return 0;
}

/* 取下标元素（内部使用，调用方保证已初始化且越界检查） */
static const config_item_t *config_item_get(uint32_t index)
{
    const config_item_t **slot = (const config_item_t **)vec_at(&s_items,
                                                                index);

    return (slot != NULL) ? *slot : NULL;
}

static struct {
    fkv_t  *kv;
    uint32_t key;
    uint32_t max_size;
    uint16_t version;
    ros_mutex_t lock;
    uint8_t  ready;
    uint8_t  lock_ready;
} s_cfg;

static int config_ready(void)
{
    return (s_cfg.ready != 0U && s_cfg.kv != NULL) ? 1 : 0;
}

/* 进入临界区；返回 1 表示已持锁，0 表示当前无需/无法加锁 */
static int config_enter(void)
{
    if (s_cfg.lock_ready != 0U &&
        ros_mutex_lock(&s_cfg.lock, ROS_TIMEOUT_FOREVER) == ROS_OK) {
        return 1;
    }
    return 0;
}

static void config_leave(int locked)
{
    if (locked != 0) {
        (void)ros_mutex_unlock(&s_cfg.lock);
    }
}

int config_init(fkv_t *kv, uint32_t key, uint16_t version, uint32_t max_size)
{
    if (kv == NULL || max_size == 0U) {
        return CONFIG_ERR_PARAM;
    }

    if (s_cfg.lock_ready == 0U && ros_mutex_init(&s_cfg.lock) == ROS_OK) {
        s_cfg.lock_ready = 1U;
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
    int locked;
    int rc;

    if (config_ready() == 0) {
        return CONFIG_ERR_PARAM;
    }

    locked = config_enter();
    rc = fkv_exists(s_cfg.kv, s_cfg.key);
    config_leave(locked);

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
    int locked;
    int rc;

    if (config_ready() == 0 || data == NULL || size == 0U) {
        return CONFIG_ERR_PARAM;
    }
    if (size > s_cfg.max_size) {
        return CONFIG_ERR_SIZE;
    }

    locked = config_enter();
    rc = fkv_get(s_cfg.kv, s_cfg.key, data, size, &len);
    config_leave(locked);

    if (rc == FKV_OK) {
        return (len == size) ? CONFIG_OK : CONFIG_ERR_SIZE;
    }
    if (rc == FKV_ERR_NOENT) {
        return CONFIG_NONE;
    }
    return (rc == FKV_ERR_NOMEM) ? CONFIG_ERR_SIZE : CONFIG_ERR_IO;
}

int config_load_raw(void *buf, uint32_t cap, uint32_t *len, uint16_t *version)
{
    uint32_t n = 0U;
    int locked;
    int rc;

    if (config_ready() == 0 || buf == NULL || cap == 0U) {
        return CONFIG_ERR_PARAM;
    }

    locked = config_enter();
    rc = fkv_get(s_cfg.kv, s_cfg.key, buf, cap, &n);
    config_leave(locked);

    if (rc == FKV_OK) {
        if (len != NULL) {
            *len = n;
        }
        if (version != NULL) {
            *version = s_cfg.version;
        }
        return CONFIG_OK;
    }
    if (rc == FKV_ERR_NOENT) {
        return CONFIG_NONE;
    }
    if (len != NULL) {
        *len = n;
    }
    return (rc == FKV_ERR_NOMEM) ? CONFIG_ERR_SIZE : CONFIG_ERR_IO;
}

int config_save(const void *data, uint32_t size)
{
    int locked;
    int rc;

    if (config_ready() == 0 || data == NULL || size == 0U) {
        return CONFIG_ERR_PARAM;
    }
    if (size > s_cfg.max_size) {
        return CONFIG_ERR_SIZE;
    }

    locked = config_enter();
    rc = fkv_put(s_cfg.kv, s_cfg.key, data, size);
    config_leave(locked);

    if (rc == FKV_OK) {
        return CONFIG_OK;
    }
    return (rc == FKV_ERR_INVAL) ? CONFIG_ERR_SIZE : CONFIG_ERR_IO;
}

int config_format(void)
{
    int locked;
    int rc;

    if (config_ready() == 0) {
        return CONFIG_ERR_PARAM;
    }

    locked = config_enter();
    rc = fkv_del(s_cfg.kv, s_cfg.key);
    config_leave(locked);

    return (rc == FKV_OK || rc == FKV_ERR_NOENT) ? CONFIG_OK : CONFIG_ERR_IO;
}

/* ============================ 配置类型注册 ============================ */

int config_register(const config_item_t *item)
{
    uint32_t i;
    uint32_t n;

    if (item == NULL || item->name == NULL || item->to_json == NULL ||
        item->from_json == NULL || item->size == 0U) {
        return CONFIG_ERR_PARAM;
    }
    if (config_items_init() != 0) {
        return CONFIG_ERR_SIZE;             /* 内存不足 */
    }
    n = vec_size(&s_items);
    for (i = 0U; i < n; i++) {
        const config_item_t *it = config_item_get(i);

        if (it == item) {
            return CONFIG_OK;               /* 同一项重复注册 */
        }
        if (it->id == item->id || strcmp(it->name, item->name) == 0) {
            return CONFIG_ERR_PARAM;        /* id 或名称冲突 */
        }
    }
    if (vec_push(&s_items, &item) != 0) {
        return CONFIG_ERR_SIZE;             /* 扩容失败 */
    }
    return CONFIG_OK;
}

const config_item_t *config_find(const char *name)
{
    uint32_t i;
    uint32_t n;

    if (name == NULL || !s_items_ready) {
        return NULL;
    }
    n = vec_size(&s_items);
    for (i = 0U; i < n; i++) {
        const config_item_t *it = config_item_get(i);

        if (it != NULL && strcmp(it->name, name) == 0) {
            return it;
        }
    }
    return NULL;
}

uint32_t config_item_count(void)
{
    return s_items_ready ? vec_size(&s_items) : 0U;
}

const config_item_t *config_item_at(uint32_t index)
{
    return s_items_ready ? config_item_get(index) : NULL;
}

/* ============================ 按键读写 ============================ */

int config_key_load(uint32_t key, void *data, uint32_t size)
{
    uint32_t len = 0U;
    int locked;
    int rc;

    if (config_ready() == 0 || data == NULL || size == 0U) {
        return CONFIG_ERR_PARAM;
    }

    locked = config_enter();
    rc = fkv_get(s_cfg.kv, key, data, size, &len);
    config_leave(locked);

    if (rc == FKV_OK) {
        return (len == size) ? CONFIG_OK : CONFIG_ERR_SIZE;
    }
    if (rc == FKV_ERR_NOENT) {
        return CONFIG_NONE;
    }
    return (rc == FKV_ERR_NOMEM) ? CONFIG_ERR_SIZE : CONFIG_ERR_IO;
}

int config_key_save(uint32_t key, const void *data, uint32_t size)
{
    int locked;
    int rc;

    if (config_ready() == 0 || data == NULL || size == 0U) {
        return CONFIG_ERR_PARAM;
    }

    locked = config_enter();
    rc = fkv_put(s_cfg.kv, key, data, size);
    config_leave(locked);

    if (rc == FKV_OK) {
        return CONFIG_OK;
    }
    return (rc == FKV_ERR_INVAL) ? CONFIG_ERR_SIZE : CONFIG_ERR_IO;
}

int config_key_delete(uint32_t key)
{
    int locked;
    int rc;

    if (config_ready() == 0) {
        return CONFIG_ERR_PARAM;
    }

    locked = config_enter();
    rc = fkv_del(s_cfg.kv, key);
    config_leave(locked);

    return (rc == FKV_OK || rc == FKV_ERR_NOENT) ? CONFIG_OK : CONFIG_ERR_IO;
}
