#ifndef RS_CMD_INTERNAL_H
#define RS_CMD_INTERNAL_H

/*
 * roshell_cmd_internal.h - roshell 命令模块内部声明
 *
 * 运行中命令登记接口（实现于 roshell_cmd.c）与内建命令查表 / help 入口
 * （实现于 roshell_builtin.c）。命令表 / 参数解析的对外 API 在 roshell.h。
 * 仅供 roshell 各实现文件使用，平台 / 组件代码不应依赖。
 */

#include "roshell.h"

struct ros_task;

/* ------------------ 命令行分派与会话查询（roshell_cmd.c） ------------------ */

/* 格式化错误输出：ERR <code>: <msg> */
void rs_error(int code, const char *fmt, ...);

/* 会话输入任务（未作为会话启动 / rs 为空时返回 NULL） */
struct ros_task *rs_session_task(const roshell_t *rs);

/* 会话启动时登记的版本串，供 ver 命令显示；rs 为空或无版本返回 NULL */
const char *rs_session_version(const roshell_t *rs);

/* ------------------ 运行中命令状态机（roshell_cmd.c） ------------------ */

/* wait=true 同步执行到结束；wait=false '-' 异步启动，返回 RS_CMD_RUNNING */
int rs_cmd_execute(roshell_t *rs, const rs_cmd_t *def, int argc, char **argv,
                   const rs_args_parsed_t *parsed, void *default_user,
                   bool wait);

/* 解析参数并执行（argv 不含命令名）；wait 语义同上 */
int rs_cmd_exec_node(roshell_t *rs, const rs_cmd_t *def, int argc,
                     char **argv, bool wait);

/* shell 任务每轮调用：推进一步本实例的异步持续命令 */
void rs_cmd_tick_all(roshell_t *rs);

/* 本实例是否存在运行中的异步持续命令（shell 据此决定空闲轮询间隔） */
bool rs_cmd_has_async(roshell_t *rs);

/* 等待本实例的异步持续命令结束；name 为 NULL 表示全部；timeout_ms=0 无限等待 */
int rs_cmd_wait_async(roshell_t *rs, const rs_cmd_t *self, const char *name,
                      uint32_t timeout_ms);

/* ------------------------------ 内建命令表（roshell_builtin.c） ------------------------------ */

/*
 * 内建命令静态表（命令表初始化时登记到全局 map）。
 * 返回首元素地址并写入条目数；count 可为 NULL。
 */
const rs_cmd_t *rs_builtin_table(uint32_t *count);

/* def 是否属于内建静态表 */
bool rs_builtin_is(const rs_cmd_t *def);

/* 按名称查找内建命令（全局命令表中命中且属于内建表，大小写敏感），未命中返回 NULL */
const rs_cmd_t *rs_builtin_find(const char *name);

/* help 内建命令：输出内建命令与 rs_cmd 全局命令表的用法 */
int rs_builtin_help(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user);

#endif /* RS_CMD_INTERNAL_H */
