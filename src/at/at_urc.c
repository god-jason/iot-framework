/*
 * URC（非请求上报）注册与分发实现。
 *
 * 职责：
 *   维护一张「行前缀 -> 回调」的固定表，供 AT 引擎在收到每一行时
 *   优先匹配 URC，从而把模组主动上报与命令响应隔离开。
 *
 * 数据流：
 *   at_urc_register() 登记前缀与回调 -> at_urc_dispatch(line, len) 顺序
 *   遍历表，用 strncmp 做行首前缀匹配 -> 命中即同步调用回调并返回。
 *
 * 静态状态与边界：
 *   - 表容量固定 AT_URC_MAX（8），满时注册失败；
 *   - 表内存由 iot_mem 在首次使用时惰性分配；
 *   - 仅保存 prefix 指针、不拷贝字符串，调用者需保证其生命周期；
 *   - 匹配只要求行首前缀相同，不要求整行相等；
 *   - 先注册者优先，前缀存在包含关系时应把更长的前缀先注册。
 */
#include "at_urc.h"
#include "iot_mem.h"
#include <string.h>

/* URC 注册表容量 */
#define AT_URC_MAX 8

/* 单个 URC 节点：回调为 NULL 表示槽位空闲 */
typedef struct {
    const char *prefix;   /* 行首匹配前缀（仅保存指针） */
    at_urc_fn callback;   /* 命中后的回调 */
    void *user;           /* 透传给回调的用户指针 */
} at_urc_node_t;

/* 全局静态注册表（首次使用时分配） */
static at_urc_node_t *s_nodes = NULL;

/* 确保注册表已分配；失败返回 false（再次调用会重试） */
static bool at_urc_ensure(void)
{
    if (s_nodes == NULL) {
        s_nodes = (at_urc_node_t *)iot_calloc(AT_URC_MAX,
                                               sizeof(at_urc_node_t));
    }
    return (s_nodes != NULL);
}

/*
 * 注册一条 URC。
 * 参数：prefix 行首前缀（非 NULL）；callback 回调（非 NULL）；user 用户指针。
 * 返回：0 成功；参数非法或表已满返回 -1。
 */
int at_urc_register(const char *prefix, at_urc_fn callback, void *user)
{
    int i;
    if (prefix == NULL || callback == NULL) {
        return -1;
    }
    if (!at_urc_ensure()) {
        return -1;   /* 内存分配失败 */
    }
    for (i = 0; i < AT_URC_MAX; i++) {
        if (s_nodes[i].callback == NULL) {
            s_nodes[i].prefix = prefix;
            s_nodes[i].callback = callback;
            s_nodes[i].user = user;
            return 0;
        }
    }
    return -1;
}

/*
 * 按前缀注销 URC。
 * 参数：prefix 已注册的前缀。
 * 返回：0 成功；参数非法或未找到返回 -1。
 */
int at_urc_unregister(const char *prefix)
{
    int i;
    if (prefix == NULL) {
        return -1;
    }
    if (s_nodes == NULL) {
        return -1;   /* 尚未分配 */
    }
    for (i = 0; i < AT_URC_MAX; i++) {
        if (s_nodes[i].callback != NULL &&
            strcmp(s_nodes[i].prefix, prefix) == 0) {
            s_nodes[i].prefix = NULL;
            s_nodes[i].callback = NULL;
            s_nodes[i].user = NULL;
            return 0;
        }
    }
    return -1;
}

/*
 * 分发一行数据到匹配的 URC 回调。
 * 参数：line 行内容（以 '\0' 结尾）；len 行长度。
 * 返回：命中并已回调返回 0；无匹配或 line 为 NULL 返回 -1。
 * 说明：使用 strncmp 只比较前缀长度，行长度不足时不会命中。
 */
int at_urc_dispatch(const char *line, uint32_t len)
{
    int i;
    if (line == NULL) {
        return -1;
    }
    if (s_nodes == NULL) {
        return -1;   /* 尚未分配：无 URC 可匹配 */
    }
    for (i = 0; i < AT_URC_MAX; i++) {
        at_urc_node_t *n = &s_nodes[i];
        uint32_t plen;

        if (n->callback == NULL) {
            continue;   /* 空闲槽位 */
        }
        plen = strlen(n->prefix);
        /* 行首前缀匹配：行长必须不小于前缀长度 */
        if (len >= plen && strncmp(line, n->prefix, plen) == 0) {
            n->callback(line, len, n->user);
            return 0;
        }
    }
    return -1;
}