/*
 * 存储后端注册表实现（按名称路由）。
 *
 * 职责：
 *   维护一张固定的后端表，把上层「按名字 + 地址 + 长度」的读写擦请求
 *   转发到对应后端驱动的 ops 函数。
 *
 * 数据流：
 *   storage_register() 登记 {名称, ops} -> storage_read/write/erase()
 *   先按名称查找 -> 再调用 ops 中对应的回调，ctx 由驱动自行保存。
 *
 * 约束与边界：
 *   - 后端表为变长数组（初始容量 STORAGE_MAX），按需扩容，内存不足时注册返回 -1；
 *   - 仅保存 name 指针，不拷贝字符串，调用者需保证名称生命周期；
 *   - 未注册、回调为空或参数非法时统一返回 -1；
 *   - 注册成功会立即调用一次 ops->init（若存在）。
 */
#include "storage.h"
#include "ros.h"
#include "common.h"
#include <string.h>

/* 注册表初始容量（按需扩容） */
#define STORAGE_MAX 8

/* 注册表节点：后端名称与操作函数表 */
typedef struct {
    const char *name;            /* 后端名称，仅保存指针，不拷贝 */
    const storage_ops_t *ops;    /* 操作函数表；为 NULL 表示槽位空闲 */
} storage_node_t;

/* 注册表（变长数组），ops 为 NULL 表示槽位空闲 */
static vec_t s_nodes;

/*
 * 惰性初始化注册表。
 * 返回：0 成功（已初始化或本次初始化成功）；-1 分配失败。
 */
static int storage_ensure(void)
{
    if (vec_data(&s_nodes) != NULL) {
        return 0;
    }
    return vec_init(&s_nodes, sizeof(storage_node_t), STORAGE_MAX);
}

/*
 * 按名称查找后端操作表。
 * 参数：name 后端名称，可为 NULL。
 * 返回：找到则返回 ops 指针，否则返回 NULL。
 */
static const storage_ops_t *storage_find(const char *name)
{
    uint32_t i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < vec_size(&s_nodes); i++) {
        storage_node_t *n = (storage_node_t *)vec_at(&s_nodes, i);
        if (n->ops != NULL && strcmp(n->name, name) == 0) {
            return n->ops;
        }
    }
    return NULL;
}

/*
 * 注册一个存储后端。
 * 参数：name 后端名称（非 NULL，调用者保证生命周期）；ops 操作表（非 NULL）。
 * 返回：0 成功；参数非法或内存不足返回 -1。
 * 说明：复用空闲槽位或追加新槽位，并立即执行一次 ops->init(ops->ctx)。
 */
int storage_register(const char *name, const storage_ops_t *ops)
{
    uint32_t i;
    storage_node_t node;

    if (name == NULL || ops == NULL) {
        return -1;
    }
    if (storage_ensure() != 0) {
        return -1;
    }
    node.name = name;
    node.ops = ops;
    for (i = 0; i < vec_size(&s_nodes); i++) {
        storage_node_t *n = (storage_node_t *)vec_at(&s_nodes, i);
        if (n->ops == NULL) {
            *n = node;                  /* 复用空闲槽位 */
            break;
        }
    }
    if (i == vec_size(&s_nodes) && vec_push(&s_nodes, &node) != 0) {
        return -1;
    }
    /* 注册即初始化，初始化返回值在此被忽略 */
    if (ops->init != NULL) {
        ops->init(ops->ctx);
    }
    return 0;
}

/*
 * 按名称注销存储后端。
 * 参数：name 后端名称。
 * 返回：0 成功；名称为 NULL 或未找到返回 -1。
 * 说明：仅清空槽位，不释放/反初始化驱动资源。
 */
int storage_unregister(const char *name)
{
    uint32_t i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < vec_size(&s_nodes); i++) {
        storage_node_t *n = (storage_node_t *)vec_at(&s_nodes, i);
        if (n->ops != NULL && strcmp(n->name, name) == 0) {
            n->name = NULL;
            n->ops = NULL;
            return 0;
        }
    }
    return -1;
}

/*
 * 从指定后端读取数据。
 * 参数：name 后端名称；addr 起始地址；data 输出缓冲；len 读取字节数。
 * 返回：后端返回值（约定 0 为成功）；名称未注册或 read 回调为空返回 -1。
 */
int storage_read(const char *name, uint32_t addr, void *data, uint32_t len)
{
    const storage_ops_t *ops = storage_find(name);
    if (ops == NULL) {
        return -1;
    }
    if (ops->read == NULL) {
        return -1;
    }
    return ops->read(ops->ctx, addr, data, len);
}

/*
 * 向指定后端写入数据。
 * 参数：name 后端名称；addr 起始地址；data 源数据；len 写入字节数。
 * 返回：后端返回值；名称未注册或 write 回调为空返回 -1。
 */
int storage_write(const char *name, uint32_t addr, const void *data, uint32_t len)
{
    const storage_ops_t *ops = storage_find(name);
    if (ops == NULL) {
        return -1;
    }
    if (ops->write == NULL) {
        return -1;
    }
    return ops->write(ops->ctx, addr, data, len);
}

/*
 * 擦除指定后端的一段区域。
 * 参数：name 后端名称；addr 起始地址；len 擦除长度（通常需按扇区对齐）。
 * 返回：后端返回值；名称未注册或 erase 回调为空返回 -1。
 */
int storage_erase(const char *name, uint32_t addr, uint32_t len)
{
    const storage_ops_t *ops = storage_find(name);
    if (ops == NULL) {
        return -1;
    }
    if (ops->erase == NULL) {
        return -1;
    }
    return ops->erase(ops->ctx, addr, len);
}