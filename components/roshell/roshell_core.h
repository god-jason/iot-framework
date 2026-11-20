#ifndef RS_CORE_H
#define RS_CORE_H

/*
 * roshell_core.h - roshell 实例状态与 roshell.c 内部导出
 *
 * 实例结构（顶层执行单元 / run 槽表 / 命令 hook）与 run 槽类型，
 * 以及 roshell.c 提供给语句解释 / 求值模块的错误填充、程序对象引用计数、
 * 函数查找与脚本扫描接口。
 * 仅供 roshell 各实现文件使用，平台 / 组件代码不应依赖。
 */

#include "roshell.h"
#include "roshell_eval.h"
#include "roshell_interp.h"

struct ros_task;

/* ------------------------------ run 槽位 ------------------------------ */

typedef struct {
    volatile uint8_t state;               /* RS_RUN_* */
    int              code;
    rs_t       *owner;
    rs_prog_t       *prog;                /* 持有一份引用 */
    struct ros_task *task;
    char             fname[RS_NAME_MAX]; /* "" 表示执行整脚本 */
    rs_value_t       args[RS_FUNC_ARGS_MAX];
    char             argstr[RS_FUNC_ARGS_MAX][RS_STR_MAX];
    uint8_t          argc;
} rs_slot_t;

/* ------------------------------ 实例 ------------------------------ */

struct roshell {
    rs_ctx_t   top;                       /* 持久顶层执行单元 */
    rs_slot_t  slots[RS_RUN_MAX];
    rs_cmd_hook_t cmd_hook;          /* 块内命令路由（规格 §8 / §9） */
    void              *cmd_hook_ctx;
    rs_prog_t         *top_prog;          /* 最近一次 rs_exec / load 登记的脚本（供 run 查找函数） */
};

/* ---------------------- 错误 / 程序对象工具 ---------------------- */

/* 无行号错误填充 */
void rs_err0(rs_err_t *e, int code, const char *msg);

/* 释放程序对象引用（refs 归零时回收） */
void rs_prog_release(rs_prog_t *p);

/* 在已登记脚本中按名查找 function，未命中返回 NULL */
const rs_fn_t *rs_find_func(const rs_prog_t *p, const char *name, uint16_t len);

/* 扫描脚本源文本（块配对校验 + 函数登记），失败返回 NULL 并填 err */
rs_prog_t *rs_scan(const char *script, rs_err_t *err);

/* 把脚本登记为会话顶层 prog（接管调用方持有的引用，供 run 查函数） */
void rs_hold_top_prog(rs_t *rs, rs_prog_t *prog);

#endif /* RS_CORE_H */
