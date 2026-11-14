#ifndef CMD_H
#define CMD_H

/*
 * cmd.h - components/cmd 命令定义与命令表分发
 *
 * cmd_def_t 描述命令（名称 / 说明 / 参数表），帮助文本由参数表动态生成；
 * 参数解析由 args.h 提供；cmd_run() 按 argv[0] 查表、解析并调用处理函数。
 * cmd_usage() / cmd_help() 生成可直接打印的用法与帮助文本。
 */

#include "args.h"
#include "vec.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CMD_NAME_MAX
#define CMD_NAME_MAX        24U     /* 命令名最大长度（含结尾 0） */
#endif

/* 命令定义（帮助文本由参数表动态生成，见 cmd_help） */
typedef struct {
    const char *name;       /* 命令名 */
    const char *desc;       /* 一句话说明 */
    const args_def_t *args;
    uint8_t     arg_count;
} cmd_def_t;

/*
 * 生成 usage 单行文本，例如：
 *   ping <host> [--count|-c <n>] [--verbose]
 * 返回写入长度；缓冲区不足返回 -1。
 */
int cmd_usage(const cmd_def_t *def, char *buf, uint32_t size);

/*
 * 生成多行帮助：说明 + usage + 参数列表。
 * 返回写入长度；缓冲区不足返回 -1。
 */
int cmd_help(const cmd_def_t *def, char *buf, uint32_t size);

/* 命令处理函数 */
typedef int (*cmd_fn_t)(const args_parsed_t *parsed, void *user);

/* 命令表条目 */
typedef struct {
    const cmd_def_t *def;
    cmd_fn_t         fn;
} cmd_entry_t;

/*
 * 在命令表中查找 argv[0] 并解析其余参数，解析成功后调用处理函数。
 * 返回处理函数返回值；未找到命令返回 ARGS_ERR_UNKNOWN；
 * 解析失败返回对应 ARGS_ERR_*。
 */
int cmd_run(const cmd_entry_t *table, uint32_t count,
            int argc, char **argv, void *user);

/* ============================== 全局命令表 ============================== */

/* 全局命令节点：def/fn/user 注册后追加到 commands 变长数组 */
typedef struct {
    const cmd_def_t *def;    /* 命令定义 */
    cmd_fn_t         fn;     /* 处理函数 */
    void            *user;   /* 处理函数上下文（由注册者提供） */
} cmd_node_t;

/* 全局命令表（定义在 cmd.c；按注册顺序追加） */
extern vec_t commands;

/*
 * 注册命令到全局命令表。
 * 名称重复（不区分大小写）或参数非法时返回 -1；成功返回 0。
 */
int cmd_register(const cmd_def_t *def, cmd_fn_t fn, void *user);

/* 清空并释放全局命令表（测试 / 反初始化用） */
void cmd_unregister_all(void);

/* 在全局命令表中查找（名称不区分大小写），未找到返回 NULL */
const cmd_node_t *cmd_find_command(const char *name);

/* 遍历全局命令表：首个节点 / 后继节点（到尾部返回 NULL） */
const cmd_node_t *cmd_first(void);
const cmd_node_t *cmd_next(const cmd_node_t *node);

/* 解析参数并执行一个已找到的命令节点 */
int cmd_run_node(const cmd_node_t *node, int argc, char **argv);

/*
 * 按 argv[0] 在全局命令表中查找并执行。
 * 未找到返回 ARGS_ERR_UNKNOWN；解析失败返回 ARGS_ERR_*；
 * 否则返回处理函数的返回值。
 */
int cmd_dispatch(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* CMD_H */
