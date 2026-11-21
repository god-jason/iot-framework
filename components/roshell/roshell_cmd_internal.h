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

/* ------------------ 运行中命令登记（rs_cmd.c） ------------------ */

/* 记录 / 解除“某任务正在执行哪条命令”，供 pause/resume/kill 查找钩子 */
void rs_cmd_active_push(struct ros_task *task, const rs_cmd_t *def);
void rs_cmd_active_pop(struct ros_task *task, const rs_cmd_t *def);

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
int rs_builtin_help(const rs_cmd_ctx_t *ctx);

#endif /* RS_CMD_INTERNAL_H */
