/*
 * KV 键值存储实现（默认 RAM 后端）。
 *
 * 职责：
 *   在内存中维护固定数量的「键 - 类型 - 值」条目，为上层提供简单的
 *   配置项/参数存取能力，不涉及掉电持久化。
 *
 * 数据流：
 *   set() 先按 key 查找，未命中则占用一个空闲槽位 -> 拷贝 key/value；
 *   get() 按 key 查找并校验类型 -> 按调用者缓冲大小做截断拷贝；
 *   delete()/clear() 仅把条目标记为空闲。
 *
 * 约束与边界：
 *   - 条目上限 STORAGE_KV_MAX（32），无 LRU 淘汰，写满后 set 失败；
 *   - key 最长 15 字符（第 16 字节为结尾 '\0'），超长会被截断；
 *   - value 最长 STORAGE_KV_VAL_MAX（32）字节，超长直接拒绝；
 *   - 仅保存原始字节，不做字符串/数值的编码转换；
 *   - 全局表在首次使用时分配，非线程安全，需由单一上下文串行访问。
 */
#include "storage_kv.h"
#include "ros_mem.h"
#include <string.h>

/* 条目数量上限 */
#define STORAGE_KV_MAX      32
/* key 缓冲长度（含结尾 '\0'） */
#define STORAGE_KV_KEY_MAX  16
/* value 最大字节数 */
#define STORAGE_KV_VAL_MAX  32

/* 单个 KV 条目：used 为 false 表示槽位空闲 */
typedef struct {
    char key[STORAGE_KV_KEY_MAX];   /* 键名，保证以 '\0' 结尾 */
    storage_kv_type_e type;         /* 值类型，读取时必须与写入一致 */
    uint8_t value[STORAGE_KV_VAL_MAX];  /* 值的原始字节 */
    uint32_t len;                   /* 值实际字节数（0..32） */
    bool used;                      /* 槽位占用标志 */
} storage_kv_entry_t;

/* 全局 KV 表指针，首次使用时分配 */
static storage_kv_entry_t *s_kv;

/*
 * 惰性分配 KV 表（清零）。
 * 返回：0 成功（已分配或本次分配成功）；-1 分配失败。
 */
static int storage_kv_ensure(void)
{
    if (s_kv != NULL) {
        return 0;
    }
    s_kv = (storage_kv_entry_t *)ros_mem_calloc(STORAGE_KV_MAX, sizeof(*s_kv));
    return (s_kv != NULL) ? 0 : -1;
}

/*
 * 按 key 查找已占用的条目。
 * 参数：key 键名，可为 NULL。
 * 返回：命中返回条目指针，否则返回 NULL。
 */
static storage_kv_entry_t *storage_kv_find(const char *key)
{
    int i;
    if (key == NULL || s_kv == NULL) {
        return NULL;
    }
    for (i = 0; i < STORAGE_KV_MAX; i++) {
        if (s_kv[i].used && strcmp(s_kv[i].key, key) == 0) {
            return &s_kv[i];
        }
    }
    return NULL;
}

/*
 * 初始化 KV 表。
 * 返回：0 成功；-1 内存分配失败。
 * 说明：先释放旧表再重新分配并清零，可重复调用。
 */
int storage_kv_init(void)
{
    int i;

    ros_mem_free(s_kv);
    s_kv = NULL;
    if (storage_kv_ensure() != 0) {
        return -1;
    }
    for (i = 0; i < STORAGE_KV_MAX; i++) {
        s_kv[i].used = false;
    }
    return 0;
}

/*
 * 写入/更新一个键值条目。
 * 参数：key 键名（非 NULL，超长截断为 15 字符）；
 *       type 值类型；value 值数据（非 NULL）；len 值字节数。
 * 返回：0 成功；参数非法、len 超过 32 或表已满返回 -1。
 * 说明：key 已存在时原地覆盖（类型与值同时更新）。
 */
int storage_kv_set(const char *key, storage_kv_type_e type,
               const void *value, uint32_t len)
{
    storage_kv_entry_t *e;
    int i;

    if (key == NULL || value == NULL) {
        return -1;
    }
    /* 值区固定 32 字节，超长无法容纳 */
    if (len > STORAGE_KV_VAL_MAX) {
        return -1;
    }
    if (storage_kv_ensure() != 0) {
        return -1;
    }

    e = storage_kv_find(key);
    if (e == NULL) {
        /* 新键：分配第一个空闲槽位 */
        for (i = 0; i < STORAGE_KV_MAX; i++) {
            if (!s_kv[i].used) {
                e = &s_kv[i];
                e->used = true;
                break;
            }
        }
        if (e == NULL) {
            return -1;   /* 表满且无可淘汰条目 */
        }
    }

    /* 拷贝键名并强制以 '\0' 结尾（超长部分被截断） */
    strncpy(e->key, key, STORAGE_KV_KEY_MAX - 1u);
    e->key[STORAGE_KV_KEY_MAX - 1u] = '\0';
    e->type = type;
    memcpy(e->value, value, len);
    e->len = len;
    return 0;
}

/*
 * 读取键值。
 * 参数：key 键名；type 期望类型（必须与写入时一致）；
 *       value 输出缓冲（非 NULL）；size 输出缓冲容量；
 *       actual_size 可选，返回条目实际字节数（可为 NULL）。
 * 返回：0 成功；键不存在、类型不匹配或参数非法返回 -1。
 * 说明：size 小于实际长度时只拷贝前 size 字节，返回的 actual_size
 *       仍是原始长度，调用者可据此判断是否被截断。
 */
int storage_kv_get(const char *key, storage_kv_type_e type,
               void *value, uint32_t size, uint32_t *actual_size)
{
    storage_kv_entry_t *e = storage_kv_find(key);
    uint32_t n;

    if (e == NULL) {
        return -1;
    }
    if (value == NULL) {
        return -1;
    }
    /* 类型不匹配视为读取失败，避免误解释内存中的字节 */
    if (type != e->type) {
        return -1;
    }
    /* 取两者较小值，防止越界写调用者缓冲 */
    n = (size < e->len) ? size : e->len;
    memcpy(value, e->value, n);
    if (actual_size != NULL) {
        *actual_size = e->len;
    }
    return 0;
}

/*
 * 删除指定键。
 * 参数：key 键名。
 * 返回：0 成功；键不存在或参数非法返回 -1。
 * 说明：只清 used 标志，数据区内容保持不变。
 */
int storage_kv_delete(const char *key)
{
    storage_kv_entry_t *e = storage_kv_find(key);
    if (e == NULL) {
        return -1;
    }
    e->used = false;
    return 0;
}

/*
 * 清空全部键值。
 * 返回：0 成功；-1 内存分配失败。
 * 说明：仅重置占用标志，不擦除数据区。
 */
int storage_kv_clear(void)
{
    int i;
    if (storage_kv_ensure() != 0) {
        return -1;
    }
    for (i = 0; i < STORAGE_KV_MAX; i++) {
        s_kv[i].used = false;
    }
    return 0;
}

/*
 * 提交（落盘）当前键值。
 * 返回：恒为 0。
 * 说明：默认 RAM 后端无需落盘，此接口为将来对接 Flash 持久化预留。
 */
int storage_kv_commit(void)
{
    /* RAM 后端无需落盘；预留持久化扩展 */
    return 0;
}