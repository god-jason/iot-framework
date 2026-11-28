#ifndef RS_EVAL_H
#define RS_EVAL_H

/*
 * roshell_eval.h - roshell 表达式求值模块内部声明
 *
 * 表达式值表示、执行单元（变量表 / 块帧栈 / token 缓冲）、求值游标，
 * 以及变量表操作、临时字符串槽与递归下降求值入口。实现见 roshell_eval.c。
 * 求值入口统一以 roshell_t *rs 为上下文（roshell_t 的 typedef 由
 * roshell_cmd.h 提供，经 roshell.h 包含链可见）。
 * 仅供 roshell 各实现文件使用，平台 / 组件代码不应依赖。
 */

#include <stdint.h>
#include <stdbool.h>

#include "vec.h"
#include "map.h"
#include "roshell_config.h"
#include "roshell_lexer.h"

/* 求值器与语句解释器共享的类型（后置定义，供跨模块原型前置声明） */
struct rs_func;
struct rs_ext_fn;

/* ------------------------------ 表达式值类型 ------------------------------ */

typedef enum {
    RS_VAL_NONE = 0,
    RS_VAL_INT,
    RS_VAL_FLOAT,
    RS_VAL_BOOL,
    RS_VAL_STRING
} rs_val_type_t;

typedef struct {
    uint8_t     type;     /* rs_val_type_t */
    int32_t     i;
    float       f;
    bool        b;
    const char *s;        /* 字符串值：指向变量槽 / 临时缓冲 / run 槽存储 */
    uint16_t    slen;
} rs_value_t;

/* ------------------------------ 执行单元 ------------------------------ */

/* 变量值盒：变量名由 map 作为键持有，值指向本结构的堆分配盒 */
typedef struct {
    uint8_t  type;
    int32_t  i;
    float    f;
    bool     b;
    char     str[RS_STR_MAX];
    uint16_t slen;
} rs_var_t;

typedef enum { RS_BLK_IF = 1, RS_BLK_FOR, RS_BLK_WHILE } rs_blk_t;

typedef struct {
    uint8_t  type;        /* rs_blk_t */
    uint16_t start;       /* 块起始行（0 基） */
    uint16_t end;         /* 对应 end 行 */
    uint16_t count;       /* FOR：列表项数 */
    uint16_t idx;         /* FOR：当前迭代下标 */
    bool     taken;       /* IF：本支是否执行 */
} rs_frame_t;

#define RS_TMP_SLOTS 4

typedef struct rs_unit {
    map_t       vars;                     /* 变量表：键=变量名，值=rs_var_t* 堆盒 */
    vec_t       toks;                     /* 单行 token 序列（容量 RS_TOKEN_MAX） */
    /* 块帧栈：栈语义、深度受 RS_BLOCK_DEPTH 约束，保留定长数组 */
    rs_frame_t  frames[RS_BLOCK_DEPTH];
    uint8_t     depth;
    uint16_t    pc;
    bool        in_func;
    bool        returned;
    rs_value_t  ret;
    char        sbuf[RS_TMP_SLOTS][RS_STR_MAX];
    uint8_t     sflip;
} rs_unit_t;

/* 释放变量表中的值盒并 deinit（执行单元销毁时调用；可重复调用） */
void rs_vars_release(rs_unit_t *ctx);

/* 表达式解析游标（求值器与隐式调用语句共用） */
typedef struct {
    const rs_tok_t *t;
    int      n;
    int      pos;
    bool     noeval;   /* 短路：跳过求值但仍消费 token */
} rs_ps_t;

/* ------------------------------ 求值接口（EXPR） ------------------------------ */

/* type mismatch 错误填充 */
int rs_type_err(roshell_t *rs, const char *msg);

/* 解析实参列表（逗号分隔表达式），argc 输出实参个数 */
int rs_p_args(roshell_t *rs, rs_ps_t *ps, rs_value_t *args, uint8_t *argc);

/* 在变量表中按名查找（不含 $）；未命中返回 NULL */
rs_var_t *rs_var_find(rs_unit_t *ctx, const char *name, uint16_t len);

/* 查找或新建变量槽；表满返回 NULL */
rs_var_t *rs_var_alloc(rs_unit_t *ctx, const char *name, uint16_t len);

/* 把求值结果写入变量槽 */
void rs_var_store(rs_var_t *v, const rs_value_t *val);

/* 临时字符串槽（ping-pong，供字面量 / 拼接 / 返回值拷贝） */
const char *rs_tmp(rs_unit_t *ctx, const char *src, uint16_t len,
                   uint16_t *out_len);

/* 求值 toks[start..] 并要求全部消费 */
int rs_eval_tokens(roshell_t *rs, const rs_tok_t *toks, int n, int start,
                   rs_value_t *out);

/* 条件求值：结果必须为布尔 */
int rs_eval_cond(roshell_t *rs, const rs_tok_t *t, int tn, int start,
                 bool *out);

/* 调用扩展函数：rs_value_t 实参转 rs_arg_t 值传递 */
int rs_ext_func_apply(roshell_t *rs, const struct rs_ext_fn *x,
                      const rs_value_t *args, uint8_t argc, rs_value_t *out);

/* 调用内置函数：校验实参个数与数值性，结果写回 out */
int rs_func_apply(roshell_t *rs, const struct rs_func *m,
                  const rs_value_t *args, uint8_t argc, rs_value_t *out);

#endif /* RS_EVAL_H */
