#ifndef RS_INTERP_H
#define RS_INTERP_H

/*
 * roshell_interp.h - roshell 语句解释模块内部声明
 *
 * 脚本程序对象（行偏移 / 函数表 / 块配对）、执行环境与按行号解释入口、
 * 函数体调用。实现见 roshell_interp.c。
 * 仅供 roshell 各实现文件使用，平台 / 组件代码不应依赖。
 */

#include "roshell.h"
#include "roshell_eval.h"

/* ------------------------------ 程序对象类型 ------------------------------ */

typedef struct {
    char     name[RS_NAME_MAX];
    uint16_t start;                       /* function 行 */
    uint8_t  argc;
    char     args[RS_FUNC_ARGS_MAX][RS_VAR_NAME_MAX];
} rs_fn_t;

typedef struct rs_prog {
    char     *text;        /* 换行替换为 \0 */
    uint16_t *line_off;    /* 每行起始偏移 */
    uint16_t *end_of;      /* 块起始行 -> end 行；非块起始为 0 */
    vec_t     funcs;       /* rs_fn_t 序列（按登记顺序） */
    map_t     func_map;    /* 函数名 -> (void*)(intptr_t)(下标+1)，0 表示无 */
    uint16_t  line_count;
    uint16_t  refs;
} rs_prog_t;

/* 执行环境：实例 + 程序对象 + 执行单元 + 调用深度 + 错误输出 */
typedef struct rs_env {
    roshell_t      *rs;
    rs_prog_t      *prog;
    rs_unit_t      *unit;                 /* 当前执行单元（顶层 / 函数体） */
    uint8_t         depth;
    rs_err_t  *err;
} rs_env_t;

/* ---------------------- 程序对象工具（roshell.c） ---------------------- */

/* 释放程序对象引用（refs 归零时回收） */
void rs_prog_release(rs_prog_t *p);

/* 在已登记脚本中按名查找 function，未命中返回 NULL */
const rs_fn_t *rs_find_func(const rs_prog_t *p, const char *name, uint16_t len);

/* 扫描脚本源文本（块配对校验 + 函数登记），失败返回 NULL 并填 err */
rs_prog_t *rs_scan(const char *script, rs_err_t *err);

/* ------------------------------ 解释接口 ------------------------------ */

/* 按行号 PC 解释执行一个执行单元（脚本 / 函数体） */
int rs_exec_unit(rs_env_t *env);

/* 函数体执行：私有变量表 + token 缓冲；返回字符串拷回 caller 临时槽 */
int rs_call_body(roshell_t *rs, rs_prog_t *prog, const rs_fn_t *f,
                 const rs_value_t *args, uint8_t depth,
                 rs_unit_t *caller, rs_value_t *out, rs_err_t *err);

#endif /* RS_INTERP_H */
