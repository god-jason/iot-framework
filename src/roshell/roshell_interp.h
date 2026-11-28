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
    char     *text;        /* 内存源：换行替换为 \0；按行源时为 NULL */
    uint16_t *line_off;    /* 每行起始偏移（仅内存源） */
    uint16_t *end_of;      /* 块起始行 -> end 行；非块起始为 0 */
    uint8_t  *line_kind;   /* 每行的 K_* 分类（rs_line_kind 结果），供跳转使用 */
    vec_t     funcs;       /* rs_fn_t 序列（按登记顺序） */
    map_t     func_map;    /* 函数名 -> (void*)(intptr_t)(下标+1)，0 表示无 */
    rs_line_src_t src;     /* 按行取源（文件等流式源）；NULL 表示内存源 */
    void     *src_ctx;     /* 取源上下文 */
    uint16_t  line_count;
    uint16_t  refs;
} rs_prog_t;

/* ---------------------- 程序对象工具（roshell.c） ---------------------- */

/* 释放程序对象引用（refs 归零时回收） */
void rs_prog_release(rs_prog_t *p);

/* 在已登记脚本中按名查找 function，未命中返回 NULL */
const rs_fn_t *rs_find_func(const rs_prog_t *p, const char *name, uint16_t len);

/* ------------------------------ 解释接口 ------------------------------ */

/* 扫描脚本源文本（块配对校验 + 函数登记），失败返回 NULL 并写 rs->err */
rs_prog_t *rs_scan(roshell_t *rs, const char *script);

/*
 * 按行源扫描（不缓存全文）：lines 为总行数（>0 时不再数行）。
 * 分两遍读源——先校验行长与行数，再逐行做块配对与函数登记。
 * 失败返回 NULL 并写 rs->err。
 */
rs_prog_t *rs_scan_src(roshell_t *rs, rs_line_src_t src, void *ctx,
                       uint16_t lines);

/*
 * 取第 idx 行的文本：内存源直接返回 text 内指针（buf 未用）；
 * 按行源读进 buf 并返回 buf。越界或读失败返回 NULL。
 */
const char *rs_prog_line(const rs_prog_t *p, uint16_t idx,
                         char *buf, uint32_t cap);

/* 扫描并按行源同步执行（不缓存全文），lines 必须为真实行数 */
int rs_exec_src(roshell_t *rs, rs_line_src_t src, void *ctx, uint16_t lines,
                rs_err_t *err);

/* 按行号 PC 解释执行当前执行单元（脚本 / 函数体） */
int rs_exec_unit(roshell_t *rs);

/* 函数体执行：私有变量表 + token 缓冲；返回字符串拷回 caller 临时槽 */
int rs_call_body(roshell_t *rs, const rs_fn_t *f,
                 const rs_value_t *args, rs_value_t *out);

#endif /* RS_INTERP_H */
