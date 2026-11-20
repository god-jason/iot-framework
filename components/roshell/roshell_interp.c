/*
 * roshell_interp.c - roshell 语句解释器（规格 §3 / §4 / §8）
 *
 * 块帧栈管理、if/for/while 控制流推进、赋值语句、隐式函数调用语句、
 * 函数体执行（rs_call_body）与按行号解释入口（rs_exec_unit）。
 */

#include "roshell.h"
#include "roshell_core.h"
#include "roshell_func.h"
#include "roshell_lexer.h"
#include "ros.h"
#include "common.h"

#include <string.h>
#include <stdio.h>

/* ============================== 运行时 ============================== */

int rs_exec_unit(rs_env_t *env);
int rs_do_load(rs_env_t *env, const char *path);

#if RS_ENABLE_EXPR

static int rs_frame_push(rs_ctx_t *ctx, uint8_t type, uint16_t start, uint16_t end)
{
    rs_frame_t *f;
    if (ctx->depth >= RS_BLOCK_DEPTH) {
        return -1;
    }
    f = &ctx->frames[ctx->depth++];
    f->type = type;
    f->start = start;
    f->end = end;
    f->count = 0U;
    f->idx = 0U;
    f->taken = false;
    return 0;
}

/* if/elseif 分支处理：本支被取则进体，否则跳到同级下一分支或 end 之后 */
static void rs_if_advance(rs_env_t *env, uint16_t hdr)
{
    rs_ctx_t *ctx = env->ctx;
    rs_frame_t *f = &ctx->frames[ctx->depth - 1U];
    uint16_t i;
    int d = 0;
    if (f->taken) {
        ctx->pc = (uint16_t)(hdr + 1U);
        return;
    }
    for (i = (uint16_t)(hdr + 1U); i < f->end; i++) {
        int kind = rs_line_kind(env->prog->text + env->prog->line_off[i]);
        if (kind == K_IF || kind == K_FOR || kind == K_WHILE || kind == K_FUNCTION) {
            d++;
        } else if (kind == K_END) {
            if (d == 0) {
                ctx->depth--;
                ctx->pc = (uint16_t)(i + 1U);
                return;
            }
            d--;
        } else if ((kind == K_ELSEIF || kind == K_ELSE) && d == 0) {
            ctx->pc = i;
            return;
        }
    }
    ctx->depth--;
    ctx->pc = (uint16_t)(f->end + 1U);
}

static int rs_stmt_assign(rs_env_t *env, const rs_tok_t *t, int tn)
{
    rs_value_t v;
    rs_var_t *var;
    int rc;
    if (rs_op(&t[1], "=")) {
        rc = rs_eval_tokens(env, t, tn, 2, &v);
        if (rc != RS_OK) {
            if (rc == RS_ERR_PARSE) {
                rs_err0(env->err, rc, "bad assignment");
            }
            return rc;
        }
        if (v.type == RS_VAL_NONE) {
            rs_err0(env->err, RS_ERR_TYPE, "no return value");
            return RS_ERR_TYPE;
        }
        var = rs_var_alloc(env->ctx, t[0].p + 1, (uint16_t)(t[0].len - 1U));
        if (var == NULL) {
            rs_err0(env->err, RS_ERR_LIMIT, "too many variables");
            return RS_ERR_LIMIT;
        }
        rs_var_store(var, &v);
        return RS_OK;
    }
    var = rs_var_find(env->ctx, t[0].p + 1, (uint16_t)(t[0].len - 1U));
    if (var == NULL) {
        rs_err0(env->err, RS_ERR_UNDEF_VAR, "undefined variable");
        return RS_ERR_UNDEF_VAR;
    }
    if (var->type != RS_VAL_INT && var->type != RS_VAL_FLOAT) {
        return rs_type_err(env, "type mismatch");
    }
    if (var->type == RS_VAL_INT) {
        var->i += rs_op(&t[1], "++") ? 1 : -1;
    } else {
        var->f += rs_op(&t[1], "++") ? 1.0f : -1.0f;
    }
    return RS_OK;
}

typedef struct { const char *p; uint16_t len; } rs_seg_t;

/* 解析 for 行：顶层逗号切出列表段（指向 prog->text 原文），可选 $i= 绑定 */
static int rs_for_parse(rs_env_t *env, const rs_tok_t *t, int tn,
                        rs_seg_t *items, uint16_t *count,
                        const char **bp, uint16_t *blen)
{
    int last = tn - 2;
    int start;
    int i;
    int d = 0;
    int seg = -1;
    uint16_t c = 0U;
    *bp = NULL;
    *blen = 0U;
    *count = 0U;
    if (!rs_kw(&t[last], "do")) {
        rs_err0(env->err, RS_ERR_PARSE, "for requires do");
        return RS_ERR_PARSE;
    }
    if (t[1].type == RS_TOK_VAR && rs_op(&t[2], "=")) {
        *bp = t[1].p + 1;
        *blen = (uint16_t)(t[1].len - 1U);
        start = 3;
    } else {
        start = 1;
    }
    for (i = start; i < last; i++) {
        if (t[i].type == RS_TOK_OP) {
            if (rs_op(&t[i], "(")) {
                d++;
            } else if (rs_op(&t[i], ")")) {
                d--;
            } else if (rs_op(&t[i], ",") && d == 0) {
                if (seg < 0) {
                    rs_err0(env->err, RS_ERR_PARSE, "for requires list");
                    return RS_ERR_PARSE;
                }
                if (c >= RS_FOR_ITEMS_MAX) {
                    rs_err0(env->err, RS_ERR_LIMIT, "too many items");
                    return RS_ERR_LIMIT;
                }
                items[c].p = t[seg].p;
                items[c].len = (uint16_t)(t[i].p - t[seg].p);
                c++;
                seg = -1;
                continue;
            }
        }
        if (seg < 0) {
            seg = i;
        }
    }
    if (seg < 0) {
        rs_err0(env->err, RS_ERR_PARSE, "for requires list");
        return RS_ERR_PARSE;
    }
    if (c >= RS_FOR_ITEMS_MAX) {
        rs_err0(env->err, RS_ERR_LIMIT, "too many items");
        return RS_ERR_LIMIT;
    }
    items[c].p = t[seg].p;
    items[c].len = (uint16_t)((t[last - 1].p + t[last - 1].len) - t[seg].p);
    *count = (uint16_t)(c + 1U);
    return RS_OK;
}

/* 惰性求值第 idx 项：原文拷入栈缓冲后重新词法（覆盖 ctx->toks 安全） */
static int rs_for_step(rs_env_t *env, const rs_tok_t *t, int tn, uint16_t idx)
{
    rs_seg_t items[RS_FOR_ITEMS_MAX];
    uint16_t count = 0U;
    const char *bp;
    uint16_t blen;
    rs_value_t v;
    char ibuf[RS_LINE_MAX];
    int rc;
    rc = rs_for_parse(env, t, tn, items, &count, &bp, &blen);
    if (rc != RS_OK || idx >= count) {
        return rc;
    }
    if (items[idx].len >= RS_LINE_MAX) {
        rs_err0(env->err, RS_ERR_LIMIT, "item too long");
        return RS_ERR_LIMIT;
    }
    memcpy(ibuf, items[idx].p, items[idx].len);
    ibuf[items[idx].len] = '\0';
    tn = rs_lex(ibuf, env->ctx->toks, RS_TOKEN_MAX);
    rc = rs_eval_tokens(env, env->ctx->toks, tn, 0, &v);
    if (rc != RS_OK) {
        return rc;
    }
    if (bp != NULL) {
        rs_var_t *var = rs_var_alloc(env->ctx, bp, blen);
        if (var == NULL) {
            rs_err0(env->err, RS_ERR_LIMIT, "too many variables");
            return RS_ERR_LIMIT;
        }
        rs_var_store(var, &v);
    }
    return RS_OK;
}

#endif /* RS_ENABLE_EXPR */

/* 隐式函数调用语句的实参解析在 EXPR 段内；rs_call_body 前向声明紧随其后 */

int rs_call_body(rs_t *rs, rs_prog_t *prog, const rs_fn_t *f,
                        const rs_value_t *args, uint8_t depth,
                        rs_ctx_t *caller, rs_value_t *out, rs_err_t *err);

#if RS_ENABLE_EXPR
/* 隐式函数调用语句：行首 WORD + '('，丢弃返回值（规格 §4.5） */
static int rs_stmt_implicit_call(rs_env_t *env, const rs_tok_t *t, int tn)
{
    rs_value_t args[RS_FUNC_ARGS_MAX];
    uint8_t argc = 0U;
    const rs_fn_t *f;
    rs_ps_t ps;
    int rc;
    ps.t = t;
    ps.n = tn;
    ps.pos = 1;
    ps.noeval = false;
    rc = rs_p_args(env, &ps, args, &argc);
    if (rc != RS_OK) {
        return rc;
    }
#if RS_ENABLE_MATH
    {
        const rs_func_t *m = rs_func_find(t[0].p, t[0].len);
        if (m != NULL) {
            rs_value_t discard;                 /* 隐式调用语句：丢弃返回值 */
            return rs_func_apply(env, m, args, argc, &discard);
        }
    }
#endif
    {
        const rs_ext_fn_t *x = rs_ext_func_find(t[0].p, t[0].len);
        if (x != NULL) {
            rs_value_t discard;
            return rs_ext_func_apply(env, x, args, argc, &discard);
        }
    }
    f = rs_find_func(env->prog, t[0].p, t[0].len);
    if (f == NULL) {
        rs_err0(env->err, RS_ERR_UNDEF_FUNC, "undefined function");
        return RS_ERR_UNDEF_FUNC;
    }
    if (argc != f->argc) {
        rs_err0(env->err, RS_ERR_ARG_COUNT, "arg count mismatch");
        return RS_ERR_ARG_COUNT;
    }
    if ((uint32_t)env->depth + 1U > RS_CALL_DEPTH) {
        rs_err0(env->err, RS_ERR_STACK_OVERFLOW, "call stack overflow");
        return RS_ERR_STACK_OVERFLOW;
    }
    return rs_call_body(env->rs, env->prog, f, args,
                        (uint8_t)(env->depth + 1U), env->ctx, NULL, env->err);
}

#endif /* RS_ENABLE_EXPR */

#if !RS_ENABLE_EXPR
/* EXPR=0 路径的无参隐式调用语句（规格 §4.5 / §6：函数退化为无参形式） */
static int rs_stmt_call0(rs_env_t *env, const rs_tok_t *t)
{
    const rs_fn_t *f;
    f = rs_find_func(env->prog, t[0].p, t[0].len);
    if (f == NULL) {
        rs_err0(env->err, RS_ERR_UNDEF_FUNC, "undefined function");
        return RS_ERR_UNDEF_FUNC;
    }
    if (f->argc != 0U) {
        rs_err0(env->err, RS_ERR_ARG_COUNT, "arg count mismatch");
        return RS_ERR_ARG_COUNT;
    }
    if ((uint32_t)env->depth + 1U > RS_CALL_DEPTH) {
        rs_err0(env->err, RS_ERR_STACK_OVERFLOW, "call stack overflow");
        return RS_ERR_STACK_OVERFLOW;
    }
    return rs_call_body(env->rs, env->prog, f, NULL,
                        (uint8_t)(env->depth + 1U), env->ctx, NULL, env->err);
}
#endif /* !RS_ENABLE_EXPR */


int rs_do_load(rs_env_t *env, const char *path)
{
#if RS_HAS_FS
    rs_prog_t *sub;
    rs_ctx_t child;
    rs_env_t cenv;
    char *buf;
    size_t got;
    FILE *fp;
    int rc;
    fp = fopen(path, "rb");
    if (fp == NULL) {
        rs_err0(env->err, RS_ERR_NOT_FOUND, "file not found");
        return RS_ERR_NOT_FOUND;
    }
    buf = (char *)ros_mem_malloc(RS_SCRIPT_MAX + 1U);
    if (buf == NULL) {
        fclose(fp);
        rs_err0(env->err, RS_ERR_RESOURCE, "out of memory");
        return RS_ERR_RESOURCE;
    }
    got = fread(buf, 1U, RS_SCRIPT_MAX, fp);
    fclose(fp);
    buf[got] = '\0';
    sub = rs_scan(buf, env->err);
    ros_mem_free(buf);
    if (sub == NULL) {
        return (env->err != NULL) ? env->err->code : RS_ERR_PARSE;
    }
    memset(&child, 0, sizeof(child));
    child.vars = env->ctx->vars;            /* 共享父变量表（规格 §4.7） */
    child.toks = (rs_tok_t *)ros_mem_malloc(RS_TOKEN_MAX * sizeof(rs_tok_t));
    if (child.toks == NULL) {
        rs_prog_release(sub);
        rs_err0(env->err, RS_ERR_RESOURCE, "out of memory");
        return RS_ERR_RESOURCE;
    }
    cenv.rs = env->rs;
    cenv.prog = sub;
    cenv.ctx = &child;
    cenv.depth = env->depth;
    cenv.err = env->err;
    rs_hold_top_prog(env->rs, sub);     /* load 的函数表保留给 run 命令 */
    sub->refs++;                        /* 执行帧自持引用：嵌套 load 替换 top_prog 不影响本次执行 */
    rc = rs_exec_unit(&cenv);
    ros_mem_free(child.toks);
    rs_prog_release(sub);
    return rc;
#else
    (void)path;
    rs_err0(env->err, RS_ERR_UNSUPPORTED, "load unsupported");
    return RS_ERR_UNSUPPORTED;
#endif
}

/* 函数体执行：私有变量表 + token 缓冲；返回字符串拷回 caller 临时槽 */
int rs_call_body(rs_t *rs, rs_prog_t *prog, const rs_fn_t *f,
                        const rs_value_t *args, uint8_t depth,
                        rs_ctx_t *caller, rs_value_t *out, rs_err_t *err)
{
    rs_ctx_t child;
    rs_env_t cenv;
    int rc;
#if RS_ENABLE_EXPR
    uint8_t i;
#endif
    memset(&child, 0, sizeof(child));
    child.vars = (rs_var_t *)ros_mem_calloc(RS_VAR_MAX, sizeof(rs_var_t));
    child.toks = (rs_tok_t *)ros_mem_malloc(RS_TOKEN_MAX * sizeof(rs_tok_t));
    if (child.vars == NULL || child.toks == NULL) {
        ros_mem_free(child.vars);
        ros_mem_free(child.toks);
        rs_err0(err, RS_ERR_RESOURCE, "out of memory");
        return RS_ERR_RESOURCE;
    }
#if RS_ENABLE_EXPR
    child.own_vars = true;
    for (i = 0U; i < f->argc; i++) {
        rs_var_t *v = rs_var_alloc(&child, f->args[i],
                                   (uint16_t)strlen(f->args[i]));
        if (v == NULL) {
            ros_mem_free(child.vars);
            ros_mem_free(child.toks);
            rs_err0(err, RS_ERR_LIMIT, "too many variables");
            return RS_ERR_LIMIT;
        }
        rs_var_store(v, &args[i]);
    }
#else
    (void)args;
    (void)caller;
    (void)out;
#endif
    cenv.rs = rs;
    cenv.prog = prog;
    cenv.ctx = &child;
    cenv.depth = depth;
    cenv.err = err;
    child.pc = (uint16_t)(f->start + 1U);
    child.in_func = true;
    rc = rs_exec_unit(&cenv);
#if RS_ENABLE_EXPR
    if (out != NULL) {
        *out = child.ret;
        if (rc != RS_OK) {
            out->type = RS_VAL_NONE;
        } else if (out->type == RS_VAL_STRING && caller != NULL) {
            out->s = rs_tmp(caller, out->s, out->slen, &out->slen);
        }
    }
#endif
    ros_mem_free(child.vars);
    ros_mem_free(child.toks);
    return rc;
}

/* 按行号 PC 解释执行一个执行单元（脚本 / 函数体 / load 子单元） */
int rs_exec_unit(rs_env_t *env)
{
    rs_ctx_t *ctx = env->ctx;
    rs_prog_t *prog = env->prog;
    rs_err_t *err = env->err;
    int rc;
    while (!ctx->returned && ctx->pc < prog->line_count) {
        uint16_t ln = ctx->pc;
        const char *line = prog->text + prog->line_off[ln];
        rs_tok_t *t = ctx->toks;
        int tn = rs_lex(line, t, RS_TOKEN_MAX);
        int kind;
        ctx->pc = (uint16_t)(ln + 1U);
        if (err != NULL) {
            err->line = (uint16_t)(ln + 1U);
        }
        if (t[0].type == RS_TOK_EOF) {
            continue;
        }
        if (t[0].type == RS_TOK_VAR) {
#if RS_ENABLE_EXPR
            rc = rs_stmt_assign(env, t, tn);
#else
            rs_err0(err, RS_ERR_UNSUPPORTED, "assignment requires expressions");
            rc = RS_ERR_UNSUPPORTED;
#endif
            if (rc != RS_OK) {
                return rc;
            }
            continue;
        }
        kind = rs_line_kind(line);
        switch (kind) {
        case K_NONE:
            if (t[0].type == RS_TOK_WORD && rs_op(&t[1], "(")) {
                /* 隐式函数调用语句（规格 §4.5）：行首已登记函数名 + '(' */
#if RS_ENABLE_EXPR
                rc = rs_stmt_implicit_call(env, t, tn);
#else
                if (t[2].type != RS_TOK_EOF) {
                    rs_err0(err, RS_ERR_UNSUPPORTED, "arguments require expressions");
                    rc = RS_ERR_UNSUPPORTED;
                } else {
                    rc = rs_stmt_call0(env, t);
                }
#endif
                if (rc != RS_OK) {
                    return rc;
                }
                break;
            }
            if (t[0].type == RS_TOK_WORD) {
                /* 命令行（规格 §8 statement -> command）：交给命令 hook 分派 */
                int handled = 0;
                if (env->rs != NULL && env->rs->cmd_hook != NULL) {
                    int cret = 0;
                    int hr = env->rs->cmd_hook(env->rs->cmd_hook_ctx, line, &cret);
                    if (hr > 0) {
                        handled = 1;
                        if (cret != 0) {
                            rs_err0(err, cret, "command failed");
                            return cret;
                        }
                    } else if (hr < 0) {
                        rs_err0(err, hr, "command error");
                        return hr;
                    }
                }
                if (handled) {
                    break;
                }
                /* 不是命令：WORD 开头无 '(' 的行继续按表达式处理（true 等） */
            }
#if RS_ENABLE_EXPR
            {
                rs_value_t v;
                rc = rs_eval_tokens(env, t, tn, 0, &v);
                if (rc == RS_ERR_PARSE) {
                    rs_err0(err, rc, "unknown statement");
                }
            }
#else
            rs_err0(err, RS_ERR_UNSUPPORTED, "expressions disabled");
            rc = RS_ERR_UNSUPPORTED;
#endif
            if (rc != RS_OK) {
                return rc;
            }
            break;
        case K_IF:
#if RS_ENABLE_EXPR
            {
                bool cond = false;
                rc = rs_eval_cond(env, t, tn, 1, &cond);
                if (rc != RS_OK) {
                    return rc;
                }
                if (rs_frame_push(ctx, RS_BLK_IF, ln, prog->end_of[ln]) != 0) {
                    rs_err0(err, RS_ERR_LIMIT, "block nesting too deep");
                    return RS_ERR_LIMIT;
                }
                ctx->frames[ctx->depth - 1U].taken = cond;
                rs_if_advance(env, ln);
            }
#else
            rs_err0(err, RS_ERR_UNSUPPORTED, "if requires expressions");
            return RS_ERR_UNSUPPORTED;
#endif
            break;
        case K_ELSEIF:
        case K_ELSE: {
            rs_frame_t *f;
            if (ctx->depth == 0U || ctx->frames[ctx->depth - 1U].type != RS_BLK_IF) {
                rs_err0(err, RS_ERR_BLOCK, "else without if");
                return RS_ERR_BLOCK;
            }
            f = &ctx->frames[ctx->depth - 1U];
            if (f->taken) {
                ctx->depth--;
                ctx->pc = (uint16_t)(f->end + 1U);
                break;
            }
            if (kind == K_ELSE) {
                f->taken = true;
                break;
            }
#if RS_ENABLE_EXPR
            rc = rs_eval_cond(env, t, tn, 1, &f->taken);
            if (rc != RS_OK) {
                return rc;
            }
            rs_if_advance(env, ln);
#else
            rs_err0(err, RS_ERR_UNSUPPORTED, "elseif requires expressions");
            return RS_ERR_UNSUPPORTED;
#endif
            break;
        }
        case K_END: {
            rs_frame_t *f;
            if (ctx->depth == 0U) {
                if (ctx->in_func) {
                    ctx->returned = true;
                    return RS_OK;
                }
                rs_err0(err, RS_ERR_BLOCK, "unmatched end");
                return RS_ERR_BLOCK;
            }
            f = &ctx->frames[ctx->depth - 1U];
            if (f->type == RS_BLK_IF) {
                ctx->depth--;
                ctx->pc = (uint16_t)(ln + 1U);
            } else if (f->type == RS_BLK_WHILE) {
                ctx->pc = f->start;
            } else {
                f->idx++;
                if (f->idx < f->count) {
                    ctx->pc = f->start;
                } else {
                    ctx->depth--;
                    ctx->pc = (uint16_t)(ln + 1U);
                }
            }
            break;
        }
        case K_FOR:
#if RS_ENABLE_EXPR
            {
                rs_frame_t *f;
                if (ctx->depth > 0U) {
                    f = &ctx->frames[ctx->depth - 1U];
                    if (f->type == RS_BLK_FOR && f->start == ln) {
                        rc = rs_for_step(env, t, tn, f->idx);
                        if (rc != RS_OK) {
                            return rc;
                        }
                        ctx->pc = (uint16_t)(ln + 1U);
                        break;
                    }
                }
                {
                    rs_seg_t items[RS_FOR_ITEMS_MAX];
                    uint16_t count = 0U;
                    const char *bp;
                    uint16_t blen;
                    rc = rs_for_parse(env, t, tn, items, &count, &bp, &blen);
                    if (rc != RS_OK) {
                        return rc;
                    }
                    if (rs_frame_push(ctx, RS_BLK_FOR, ln, prog->end_of[ln]) != 0) {
                        rs_err0(err, RS_ERR_LIMIT, "block nesting too deep");
                        return RS_ERR_LIMIT;
                    }
                    ctx->frames[ctx->depth - 1U].count = count;
                }
                rc = rs_for_step(env, t, tn, 0U);
                if (rc != RS_OK) {
                    return rc;
                }
                ctx->pc = (uint16_t)(ln + 1U);
            }
#else
            rs_err0(err, RS_ERR_UNSUPPORTED, "for requires expressions");
            return RS_ERR_UNSUPPORTED;
#endif
            break;
        case K_WHILE:
#if RS_ENABLE_EXPR
            {
                bool cond = false;
                bool reentry = (ctx->depth > 0U &&
                                ctx->frames[ctx->depth - 1U].type == RS_BLK_WHILE &&
                                ctx->frames[ctx->depth - 1U].start == ln);
                rc = rs_eval_cond(env, t, tn, 1, &cond);
                if (rc != RS_OK) {
                    return rc;
                }
                if (cond) {
                    if (!reentry &&
                        rs_frame_push(ctx, RS_BLK_WHILE, ln, prog->end_of[ln]) != 0) {
                        rs_err0(err, RS_ERR_LIMIT, "block nesting too deep");
                        return RS_ERR_LIMIT;
                    }
                } else {
                    if (reentry) {
                        ctx->depth--;
                    }
                    ctx->pc = (uint16_t)(prog->end_of[ln] + 1U);
                }
            }
#else
            rs_err0(err, RS_ERR_UNSUPPORTED, "while requires expressions");
            return RS_ERR_UNSUPPORTED;
#endif
            break;
        case K_BREAK: {
            int k = -1;
            uint8_t j;
            uint16_t be;
            for (j = ctx->depth; j > 0U; j--) {
                if (ctx->frames[j - 1U].type != RS_BLK_IF) {
                    k = (int)(j - 1U);
                    break;
                }
            }
            if (k < 0) {
                rs_err0(err, RS_ERR_PARSE, "break outside loop");
                return RS_ERR_PARSE;
            }
            be = ctx->frames[k].end;
            ctx->depth = (uint8_t)k;
            ctx->pc = (uint16_t)(be + 1U);
            break;
        }
        case K_FUNCTION:
            ctx->pc = (uint16_t)(prog->end_of[ln] + 1U);
            break;
        case K_RETURN:
            if (!ctx->in_func) {
                rs_err0(err, RS_ERR_PARSE, "return outside function");
                return RS_ERR_PARSE;
            }
            if (t[1].type != RS_TOK_EOF) {
#if RS_ENABLE_EXPR
                rc = rs_eval_tokens(env, t, tn, 1, &ctx->ret);
                if (rc != RS_OK) {
                    return rc;
                }
#else
                rs_err0(err, RS_ERR_UNSUPPORTED, "return requires expressions");
                return RS_ERR_UNSUPPORTED;
#endif
            }
            ctx->returned = true;
            break;
        default:
            rs_err0(err, RS_ERR_PARSE, "unknown statement");
            return RS_ERR_PARSE;
        }
    }
    return RS_OK;
}
