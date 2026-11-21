/*
 * roshell.c - roshell 实例生命周期与公开 API
 *
 * create / destroy、脚本加载扫描（rs_scan：块配对校验 + 函数登记）、
 * 同步执行（rs_exec / exec_line）、run 异步任务槽管理、
 * 顶层变量读取与错误 / 程序对象工具。
 * 词法 / 求值 / 语句解释分别在 roshell_lexer.c / roshell_eval.c /
 * roshell_interp.c，内置函数表在 roshell_func.c。
 */

#include "roshell.h"
#include "roshell_core.h"
#include "roshell_func.h"
#include "roshell_lexer.h"
#include "ros.h"
#include "common.h"

#include <string.h>

static void rs_err(rs_err_t *e, int code, uint16_t line, const char *msg)
{
    if (e == NULL) {
        return;
    }
    e->code = code;
    e->line = line;
    strncpy(e->msg, msg, sizeof(e->msg) - 1U);
    e->msg[sizeof(e->msg) - 1U] = '\0';
}

/* ============================== 错误 / 程序对象 ============================== */

void rs_err0(rs_err_t *e, int code, const char *msg)
{
    if (e == NULL) {
        return;
    }
    e->code = code;
    strncpy(e->msg, msg, sizeof(e->msg) - 1U);
    e->msg[sizeof(e->msg) - 1U] = '\0';
}

static void rs_prog_free(rs_prog_t *p)
{
    if (p == NULL) {
        return;
    }
    vec_deinit(&p->funcs);
    map_deinit(&p->func_map);
    if (p->text != NULL) {
        ros_mem_free(p->text);
    }
    if (p->line_off != NULL) {
        ros_mem_free(p->line_off);
    }
    if (p->end_of != NULL) {
        ros_mem_free(p->end_of);
    }
    ros_mem_free(p);
}

void rs_prog_release(rs_prog_t *p)
{
    if (p != NULL && --p->refs == 0U) {
        rs_prog_free(p);
    }
}

const rs_fn_t *rs_find_func(const rs_prog_t *p, const char *name, uint16_t len)
{
    char key[RS_NAME_MAX];
    intptr_t idx;

    if (p == NULL || name == NULL || len == 0U || len >= RS_NAME_MAX) {
        return NULL;
    }
    memcpy(key, name, len);
    key[len] = '\0';
    idx = (intptr_t)map_get_ptr(&p->func_map, key);
    if (idx <= 0) {
        return NULL;
    }
    return (const rs_fn_t *)vec_at(&p->funcs, (uint32_t)(idx - 1));
}
/* ============================== 加载扫描（轻量校验 + 函数登记） ============================== */

typedef struct {
    int      kind;
    uint16_t line;
    bool     has_else;
} rs_pend_t;

rs_prog_t *rs_scan(const char *script, rs_err_t *err)
{
    rs_prog_t *p;
    vec_t toks;                           /* 行 token 暂存：容量 RS_TOKEN_MAX，逐行复用 */
    rs_pend_t pend[RS_BLOCK_DEPTH];
    uint8_t depth = 0U;
    size_t len;
    size_t i;
    uint16_t nlines = 0U;
    uint16_t k;
    memset(&toks, 0, sizeof(toks));
    len = strlen(script);
    if (len > RS_SCRIPT_MAX) {
        rs_err(err, RS_ERR_LIMIT, 0U, "script too long");
        return NULL;
    }
    if (len > 0U) {
        nlines = 1U;
        for (i = 0U; i < len; i++) {
            if (script[i] == '\n') {
                nlines++;
            }
        }
        if (script[len - 1U] == '\n') {
            nlines--;
        }
    }
    p = (rs_prog_t *)ros_mem_calloc(1U, sizeof(*p));
    if (p != NULL) {
        p->text = (char *)ros_mem_malloc(len + 1U);
        p->line_off = (uint16_t *)ros_mem_malloc((size_t)(nlines + 1U) * sizeof(uint16_t));
        p->end_of = (uint16_t *)ros_mem_calloc((size_t)(nlines + 1U), sizeof(uint16_t));
    }
    if (p == NULL || p->text == NULL ||
        p->line_off == NULL || p->end_of == NULL ||
        vec_init(&toks, sizeof(rs_tok_t), RS_TOKEN_MAX) != 0 ||
        vec_init(&p->funcs, sizeof(rs_fn_t), 0U) != 0 ||
        map_init(&p->func_map) != 0) {
        rs_err(err, RS_ERR_RESOURCE, 0U, "out of memory");
        vec_deinit(&toks);
        rs_prog_free(p);
        return NULL;
    }
    p->line_count = nlines;
    /* 切行：换行/回车替换为 \0，行偏移表指向 text */
    i = 0U;
    for (k = 0U; k < nlines; k++) {
        size_t start = i;
        p->line_off[k] = (uint16_t)start;
        while (i < len && script[i] != '\n') {
            p->text[i] = (script[i] == '\r') ? '\0' : script[i];
            i++;
        }
        p->text[i] = '\0';
        if (i - start > (size_t)RS_LINE_MAX - 1U) {
            rs_err(err, RS_ERR_LIMIT, (uint16_t)(k + 1U), "line too long");
            goto fail;
        }
        if (i < len) {
            i++;                          /* 跳过换行 */
        }
    }
    for (k = 0U; k < nlines; k++) {
        const char *line = p->text + p->line_off[k];
        int tn = rs_lex(line, &toks);
        const rs_tok_t *t = (const rs_tok_t *)vec_data(&toks);
        int kind;
        int last = tn - 2;                /* 末个实 token 下标 */
        if (t[0].type == RS_TOK_EOF) {
            continue;                     /* 空行 / 纯注释行 */
        }
        if (t[0].type == RS_TOK_VAR) {    /* 赋值 / 后缀自增减语句 */
#if RS_ENABLE_EXPR
            if (rs_op(&t[1], "=")) {
                if (t[2].type == RS_TOK_EOF) {
                    rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "bad assignment");
                    goto fail;
                }
            } else if (rs_op(&t[1], "++") || rs_op(&t[1], "--")) {
                if (t[2].type != RS_TOK_EOF) {
                    rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "bad assignment");
                    goto fail;
                }
            } else {
                rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "bad assignment");
                goto fail;
            }
#else
            rs_err(err, RS_ERR_UNSUPPORTED, (uint16_t)(k + 1U),
                   "assignment requires expressions");
            goto fail;
#endif
            continue;
        }
        if (t[0].type != RS_TOK_WORD) {   /* 表达式语句 */
#if !RS_ENABLE_EXPR
            rs_err(err, RS_ERR_UNSUPPORTED, (uint16_t)(k + 1U),
                   "expressions disabled");
            goto fail;
#else
            (void)last;
#endif
            continue;
        }
        kind = rs_line_kind(line);
        switch (kind) {
        case K_IF:
#if RS_ENABLE_EXPR
            if (t[1].type == RS_TOK_EOF) {
                rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "if requires condition");
                goto fail;
            }
            if (depth >= RS_BLOCK_DEPTH) {
                rs_err(err, RS_ERR_LIMIT, (uint16_t)(k + 1U), "block nesting too deep");
                goto fail;
            }
            pend[depth].kind = K_IF;
            pend[depth].line = k;
            pend[depth].has_else = false;
            depth++;
#else
            rs_err(err, RS_ERR_UNSUPPORTED, (uint16_t)(k + 1U), "if requires expressions");
            goto fail;
#endif
            break;
        case K_FOR:
#if RS_ENABLE_EXPR
            if (!rs_kw(&t[last], "do")) {
                rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "for requires do");
                goto fail;
            }
            {
                int start = (t[1].type == RS_TOK_VAR && rs_op(&t[2], "=")) ? 3 : 1;
                if (start > last - 1) {
                    rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "for requires list");
                    goto fail;
                }
            }
            if (depth >= RS_BLOCK_DEPTH) {
                rs_err(err, RS_ERR_LIMIT, (uint16_t)(k + 1U), "block nesting too deep");
                goto fail;
            }
            pend[depth].kind = K_FOR;
            pend[depth].line = k;
            pend[depth].has_else = false;
            depth++;
#else
            rs_err(err, RS_ERR_UNSUPPORTED, (uint16_t)(k + 1U), "for requires expressions");
            goto fail;
#endif
            break;
        case K_WHILE:
#if RS_ENABLE_EXPR
            if (t[1].type == RS_TOK_EOF) {
                rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "while requires condition");
                goto fail;
            }
            if (depth >= RS_BLOCK_DEPTH) {
                rs_err(err, RS_ERR_LIMIT, (uint16_t)(k + 1U), "block nesting too deep");
                goto fail;
            }
            pend[depth].kind = K_WHILE;
            pend[depth].line = k;
            pend[depth].has_else = false;
            depth++;
#else
            rs_err(err, RS_ERR_UNSUPPORTED, (uint16_t)(k + 1U), "while requires expressions");
            goto fail;
#endif
            break;
        case K_FUNCTION: {
            uint16_t j;
            uint8_t argc = 0U;
            uint8_t c;
            uint32_t idx;
            rs_fn_t fn;
            if (t[1].type != RS_TOK_WORD || t[1].len >= RS_NAME_MAX) {
                rs_err(err, RS_ERR_LIMIT, (uint16_t)(k + 1U), "bad function name");
                goto fail;
            }
            if (!rs_op(&t[2], "(")) {
                rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "bad function header");
                goto fail;
            }
            j = 3U;
            while (t[j].type == RS_TOK_VAR) {
                if (argc >= RS_FUNC_ARGS_MAX) {
                    rs_err(err, RS_ERR_LIMIT, (uint16_t)(k + 1U), "too many arguments");
                    goto fail;
                }
                if (t[j].len >= RS_VAR_NAME_MAX) {
                    rs_err(err, RS_ERR_LIMIT, (uint16_t)(k + 1U), "parameter name too long");
                    goto fail;
                }
                argc++;
                if (!rs_op(&t[j + 1U], ",")) {
                    j++;
                    break;
                }
                j += 2U;
            }
            if (!rs_op(&t[j], ")") || (int)j != last) {
                rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "bad function header");
                goto fail;
            }
#if !RS_ENABLE_EXPR
            if (argc > 0U) {
                rs_err(err, RS_ERR_UNSUPPORTED, (uint16_t)(k + 1U),
                       "function arguments require expressions");
                goto fail;
            }
#endif
            if (rs_find_func(p, t[1].p, t[1].len) != NULL) {
                rs_err(err, RS_ERR_DUPLICATE, (uint16_t)(k + 1U), "duplicate function");
                goto fail;
            }
#if RS_ENABLE_MATH
            /* 内置函数 / 常量名保留，不允许被 function 重定义 */
            if (rs_func_find(t[1].p, t[1].len) != NULL ||
                rs_const_find(t[1].p, t[1].len) != NULL) {
                rs_err(err, RS_ERR_DUPLICATE, (uint16_t)(k + 1U), "duplicate function");
                goto fail;
            }
#endif
#if RS_ENABLE_EXPR
            /* 扩展注册函数 / 常量名同样保留 */
            if (rs_ext_func_find(t[1].p, t[1].len) != NULL ||
                rs_ext_const_find(t[1].p, t[1].len) != NULL) {
                rs_err(err, RS_ERR_DUPLICATE, (uint16_t)(k + 1U), "duplicate function");
                goto fail;
            }
#endif
            if (vec_size(&p->funcs) >= RS_FUNC_MAX) {
                rs_err(err, RS_ERR_LIMIT, (uint16_t)(k + 1U), "too many functions");
                goto fail;
            }
            memset(&fn, 0, sizeof(fn));
            memcpy(fn.name, t[1].p, t[1].len);
            fn.name[t[1].len] = '\0';
            fn.start = k;
            fn.argc = argc;
            j = 3U;
            for (c = 0U; c < argc; c++) {
                memcpy(fn.args[c], t[j].p + 1, t[j].len - 1U);
                fn.args[c][t[j].len - 1U] = '\0';
                j += rs_op(&t[j + 1U], ",") ? 2U : 1U;
            }
            idx = vec_size(&p->funcs);
            if (vec_push(&p->funcs, &fn) != 0 ||
                !map_put(&p->func_map, fn.name,
                         (void *)(intptr_t)(idx + 1U))) {
                rs_err(err, RS_ERR_RESOURCE, (uint16_t)(k + 1U), "out of memory");
                goto fail;
            }
            if (depth >= RS_BLOCK_DEPTH) {
                rs_err(err, RS_ERR_LIMIT, (uint16_t)(k + 1U), "block nesting too deep");
                goto fail;
            }
            pend[depth].kind = K_FUNCTION;
            pend[depth].line = k;
            pend[depth].has_else = false;
            depth++;
            break;
        }
        case K_ELSEIF:
        case K_ELSE:
            if (depth == 0U || pend[depth - 1U].kind != K_IF) {
                rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U),
                       (kind == K_ELSEIF) ? "elseif without if" : "else without if");
                goto fail;
            }
            if (kind == K_ELSEIF) {
                if (pend[depth - 1U].has_else) {
                    rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "elseif after else");
                    goto fail;
                }
#if RS_ENABLE_EXPR
                if (t[1].type == RS_TOK_EOF) {
                    rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "elseif requires condition");
                    goto fail;
                }
#endif
            } else {
                if (pend[depth - 1U].has_else) {
                    rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "duplicate else");
                    goto fail;
                }
                pend[depth - 1U].has_else = true;
            }
            break;
        case K_END:
            if (depth == 0U) {
                rs_err(err, RS_ERR_BLOCK, (uint16_t)(k + 1U), "unmatched end");
                goto fail;
            }
            depth--;
            p->end_of[pend[depth].line] = k;
            break;
        case K_BREAK: {
            uint8_t j;
            bool found = false;
            for (j = 0U; j < depth; j++) {
                if (pend[j].kind == K_FOR || pend[j].kind == K_WHILE) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "break outside loop");
                goto fail;
            }
            break;
        }
        case K_RETURN: {
            uint8_t j;
            bool found = false;
            for (j = 0U; j < depth; j++) {
                if (pend[j].kind == K_FUNCTION) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                rs_err(err, RS_ERR_PARSE, (uint16_t)(k + 1U), "return outside function");
                goto fail;
            }
            break;
        }
        default:                           /* 非关键字 WORD：隐式调用语句或命令行 */
            if (rs_op(&t[1], "(")) {
                /* 隐式函数调用语句（规格 §4.5）：执行期按名解析（允许前向引用） */
#if !RS_ENABLE_EXPR
                if (tn != 4) {
                    rs_err(err, RS_ERR_UNSUPPORTED, (uint16_t)(k + 1U),
                           "arguments require expressions");
                    goto fail;
                }
#endif
            }
            /* 其余 WORD 行视为命令行（规格 §8 statement -> command）：
             * 命令表不在引擎内，扫描期放行，运行期由 hook 分派并校验 */
            break;
        }
    }
    if (depth > 0U) {
        rs_err(err, RS_ERR_BLOCK, (uint16_t)(pend[depth - 1U].line + 1U), "unclosed block");
        goto fail;
    }
    vec_deinit(&toks);
    p->refs = 1U;
    return p;
fail:
    vec_deinit(&toks);
    rs_prog_free(p);
    return NULL;
}

/* 把脚本 prog 登记为会话顶层 prog（接管调用方持有的引用）：
 * 函数在 rs_exec 结束后仍保留，供 run 命令查找（规格 §9 会话示例）。
 * 运行中的 run 任务另持引用，替换不会释放正在使用的脚本。 */
void rs_hold_top_prog(rs_t *rs, rs_prog_t *prog)
{
    if (rs->top_prog == prog) {
        return;
    }
    if (rs->top_prog != NULL) {
        rs_prog_release(rs->top_prog);
    }
    rs->top_prog = prog;
}

/* ============================== run 槽位 / 任务 ============================== */

static void rs_slot_reap(rs_slot_t *s)
{
    if (s->state == RS_RUN_RUNNING || s->task == NULL) {
        return;
    }
    if (s->task->state == ROS_TASK_TERMINATED) {
        ros_task_destroy(s->task);
        s->task = NULL;
        s->state = RS_RUN_FREE;
        s->fname[0] = '\0';
        list_remove(&s->link);              /* 归还槽位：摘除活跃表 */
    }
}

static void rs_run_entry(void *arg)
{
    rs_slot_t *s = (rs_slot_t *)arg;
    rs_err_t e;
    rs_ctx_t ctx;
    rs_env_t env;
    int rc;
    memset(&e, 0, sizeof(e));
    memset(&ctx, 0, sizeof(ctx));
    env.rs = s->owner;
    env.prog = s->prog;
    env.ctx = &ctx;
    env.depth = 0U;
    env.err = &e;
    if (s->fname[0] != '\0') {
        const rs_fn_t *f = rs_find_func(s->prog, s->fname,
                                          (uint16_t)strlen(s->fname));
        if (f == NULL) {
            rc = RS_ERR_UNDEF_FUNC;
        } else if (f->argc != s->argc) {
            rc = RS_ERR_ARG_COUNT;
        } else {
            rc = rs_call_body(s->owner, s->prog, f, s->args, 0U, NULL, NULL, &e);
        }
    } else {
        if (vec_init(&ctx.toks, sizeof(rs_tok_t), RS_TOKEN_MAX) != 0) {
            rc = RS_ERR_RESOURCE;
        } else {
            rc = rs_exec_unit(&env);
        }
        rs_vars_release(&ctx);
        vec_deinit(&ctx.toks);
    }
    s->code = rc;
    s->state = (rc == RS_OK) ? RS_RUN_DONE : RS_RUN_ERROR;
    rs_prog_release(s->prog);
    s->prog = NULL;
    ros_task_exit();
}

static int rs_slot_start(rs_t *rs, rs_prog_t *prog, const char *fname,
                         uint16_t flen, const rs_value_t *args, uint8_t argc)
{
    rs_slot_t *s = NULL;
    ros_task_t *task;
    uint32_t i;
    uint32_t free_idx = RS_RUN_MAX;
    for (i = 0U; i < RS_RUN_MAX; i++) {
        rs_slot_reap(&rs->slots[i]);
        if (s == NULL && rs->slots[i].state == RS_RUN_FREE) {
            s = &rs->slots[i];
            free_idx = i;
        }
    }
    if (s == NULL) {
        return RS_ERR_LIMIT;
    }
    memset(s, 0, sizeof(*s));
    list_node_init(&s->link);               /* memset 后恢复游离态，待占槽成功后入表 */
    s->owner = rs;
    if (flen > 0U && flen < RS_NAME_MAX) {
        memcpy(s->fname, fname, flen);
        s->fname[flen] = '\0';
    }
    for (i = 0U; i < argc; i++) {
        s->args[i] = args[i];
        if (args[i].type == RS_VAL_STRING) {
            memcpy(s->argstr[i], args[i].s, args[i].slen);
            s->argstr[i][args[i].slen] = '\0';
            s->args[i].s = s->argstr[i];
        }
    }
    s->argc = argc;
    task = ros_task_create_dynamic("roshell", RS_RUN_PRIO, rs_run_entry,
                                   s, RS_RUN_STACK);
    if (task == NULL) {
        return RS_ERR_RESOURCE;
    }
    s->task = task;
    s->prog = prog;
    prog->refs++;
    s->state = RS_RUN_RUNNING;
    list_add_tail(&rs->active_list, &s->link);   /* 占槽成功：登记活跃表 */
    return (int)free_idx;
}
/* ============================== 公开 API ============================== */

rs_t *rs_create(void)
{
    uint32_t i;
    rs_t *rs = (rs_t *)ros_mem_calloc(1U, sizeof(*rs));
    if (rs == NULL) {
        return NULL;
    }
    list_init(&rs->active_list);
    for (i = 0U; i < RS_RUN_MAX; i++) {
        list_node_init(&rs->slots[i].link);
    }
    if (vec_init(&rs->top.toks, sizeof(rs_tok_t), RS_TOKEN_MAX) != 0) {
        rs_destroy(rs);
        return NULL;
    }
    return rs;
}

void rs_destroy(rs_t *rs)
{
    list_node_t *pos;
    if (rs == NULL) {
        return;
    }
    pos = list_first(&rs->active_list);
    while (pos != NULL && pos != &rs->active_list.head) {
        rs_slot_t *s = ROS_CONTAINER_OF(pos, rs_slot_t, link);
        list_node_t *next = list_next(pos);

        if (s->state == RS_RUN_RUNNING) {
            pos = next;                     /* 运行中任务：跳过，由任务自行释放 */
            continue;
        }
        list_remove(&s->link);
        if (s->task != NULL) {
            if (s->task->state == ROS_TASK_TERMINATED &&
                ros_task_destroy(s->task) == ROS_OK) {
                s->task = NULL;
            }
        }
        if (s->prog != NULL) {
            rs_prog_release(s->prog);
            s->prog = NULL;
        }
        pos = next;
    }
    if (rs->top_prog != NULL) {
        rs_prog_release(rs->top_prog);
        rs->top_prog = NULL;
    }
    rs_vars_release(&rs->top);
    vec_deinit(&rs->top.toks);
    ros_mem_free(rs);
}

int rs_exec(rs_t *rs, const char *script, rs_err_t *err)
{
    rs_prog_t *prog;
    rs_env_t env;
    int rc;
    if (rs == NULL || script == NULL) {
        rs_err(err, RS_ERR_PARAM, 0U, "bad parameter");
        return RS_ERR_PARAM;
    }
    prog = rs_scan(script, err);
    if (prog == NULL) {
        return (err != NULL) ? err->code : RS_ERR_PARSE;
    }
    rs->top.pc = 0U;
    rs->top.depth = 0U;
    rs->top.returned = false;
    rs->top.in_func = false;
    memset(&rs->top.ret, 0, sizeof(rs->top.ret));
    env.rs = rs;
    env.prog = prog;
    env.ctx = &rs->top;
    env.depth = 0U;
    env.err = err;
    /* 登记为会话顶层脚本：其中定义的函数在执行后仍保留给 run 命令 */
    rs_hold_top_prog(rs, prog);
    prog->refs++;                         /* 执行帧自持引用 */
    rc = rs_exec_unit(&env);
    rs_prog_release(prog);
    return rc;
}

int rs_exec_line(rs_t *rs, const char *line, rs_err_t *err)
{
    if (rs == NULL || line == NULL) {
        rs_err(err, RS_ERR_PARAM, 0U, "bad parameter");
        return RS_ERR_PARAM;
    }
    switch (rs_line_kind(line)) {
    case K_IF: case K_ELSEIF: case K_ELSE: case K_END:
    case K_FOR: case K_WHILE: case K_FUNCTION:
        rs_err(err, RS_ERR_PARSE, 1U, "block statement requires script");
        return RS_ERR_PARSE;
    default:
        break;
    }
    return rs_exec(rs, line, err);
}

void rs_set_cmd_hook(rs_t *rs, rs_cmd_hook_t hook, void *ctx)
{
    if (rs == NULL) {
        return;
    }
    rs->cmd_hook = hook;
    rs->cmd_hook_ctx = ctx;
}

int rs_run_async(rs_t *rs, const char *fn,
                      const rs_arg_t *args, int nargs)
{
    rs_prog_t *prog;
    const rs_fn_t *f;
    rs_value_t vargs[RS_FUNC_ARGS_MAX];
    int slot;
    int i;
    if (rs == NULL || fn == NULL || nargs < 0 ||
        (nargs > 0 && args == NULL) ||
        nargs > (int)RS_FUNC_ARGS_MAX) {
        return RS_ERR_PARAM;
    }
    prog = rs->top_prog;
    if (prog == NULL) {
        return RS_ERR_UNDEF_FUNC;
    }
    f = rs_find_func(prog, fn, strlen(fn));
    if (f == NULL) {
        return RS_ERR_UNDEF_FUNC;
    }
    if ((uint8_t)nargs != f->argc) {
        return RS_ERR_ARG_COUNT;
    }
    for (i = 0; i < nargs; i++) {
        memset(&vargs[i], 0, sizeof(vargs[i]));
        switch (args[i].type) {
        case RS_ARG_INT:
            vargs[i].type = RS_VAL_INT;
            vargs[i].i = args[i].i;
            break;
        case RS_ARG_FLOAT:
            vargs[i].type = RS_VAL_FLOAT;
            vargs[i].f = args[i].f;
            break;
        case RS_ARG_BOOL:
            vargs[i].type = RS_VAL_BOOL;
            vargs[i].b = args[i].b;
            break;
        case RS_ARG_STR:
            vargs[i].type = RS_VAL_STRING;
            vargs[i].s = args[i].s;
            vargs[i].slen = args[i].slen;
            break;
        default:
            return RS_ERR_PARAM;
        }
    }
    slot = rs_slot_start(rs, prog, fn, strlen(fn), vargs, (uint8_t)nargs);
    return slot;
}

int rs_run_wait(rs_t *rs, int slot, uint32_t timeout_ms)
{
    rs_slot_t *s;
    uint32_t waited = 0U;
    uint8_t st;
    int code;
    if (rs == NULL || slot < 0 || (uint32_t)slot >= RS_RUN_MAX) {
        return RS_ERR_PARAM;
    }
    s = &rs->slots[slot];
    while (s->state == RS_RUN_RUNNING) {
        if (timeout_ms != ROS_TIMEOUT_FOREVER && waited >= timeout_ms) {
            return RS_ERR_TIMEOUT;
        }
        ros_sleep_ms(1U);
        waited++;
    }
    st = s->state;
    code = s->code;
    rs_slot_reap(s);
    if (st == RS_RUN_DONE) {
        return RS_OK;
    }
    if (st == RS_RUN_ERROR) {
        return code;
    }
    return RS_ERR_PARAM;
}

int rs_run_state(const rs_t *rs, int slot)
{
    if (rs == NULL || slot < 0 || (uint32_t)slot >= RS_RUN_MAX) {
        return RS_ERR_PARAM;
    }
    return (int)rs->slots[slot].state;
}

int rs_run_count(const rs_t *rs)
{
    if (rs == NULL) {
        return 0;
    }
    return (int)list_size(&rs->active_list);
}

const char *rs_run_fname(const rs_t *rs, int slot)
{
    if (rs == NULL || slot < 0 || (uint32_t)slot >= RS_RUN_MAX) {
        return "";
    }
    return rs->slots[slot].fname;
}

/* ============================== 顶层变量读取 ============================== */

#if RS_ENABLE_EXPR
static rs_var_t *rs_top_var(rs_t *rs, const char *name)
{
    if (name[0] == '$') {
        name++;
    }
    return rs_var_find(&rs->top, name, (uint16_t)strlen(name));
}

int rs_get_int(rs_t *rs, const char *name, int32_t *out)
{
    rs_var_t *v;
    if (rs == NULL || name == NULL || out == NULL) {
        return RS_ERR_PARAM;
    }
    v = rs_top_var(rs, name);
    if (v == NULL) {
        return RS_ERR_UNDEF_VAR;
    }
    if (v->type != RS_VAL_INT) {
        return RS_ERR_TYPE;
    }
    *out = v->i;
    return RS_OK;
}

int rs_get_float(rs_t *rs, const char *name, float *out)
{
    rs_var_t *v;
    if (rs == NULL || name == NULL || out == NULL) {
        return RS_ERR_PARAM;
    }
    v = rs_top_var(rs, name);
    if (v == NULL) {
        return RS_ERR_UNDEF_VAR;
    }
    if (v->type != RS_VAL_FLOAT) {
        return RS_ERR_TYPE;
    }
    *out = v->f;
    return RS_OK;
}

int rs_get_bool(rs_t *rs, const char *name, bool *out)
{
    rs_var_t *v;
    if (rs == NULL || name == NULL || out == NULL) {
        return RS_ERR_PARAM;
    }
    v = rs_top_var(rs, name);
    if (v == NULL) {
        return RS_ERR_UNDEF_VAR;
    }
    if (v->type != RS_VAL_BOOL) {
        return RS_ERR_TYPE;
    }
    *out = v->b;
    return RS_OK;
}

int rs_get_str(rs_t *rs, const char *name, char *buf, uint32_t cap)
{
    rs_var_t *v;
    uint32_t n;
    if (rs == NULL || name == NULL || buf == NULL || cap == 0U) {
        return RS_ERR_PARAM;
    }
    v = rs_top_var(rs, name);
    if (v == NULL) {
        return RS_ERR_UNDEF_VAR;
    }
    if (v->type != RS_VAL_STRING) {
        return RS_ERR_TYPE;
    }
    n = v->slen;
    if (n > cap - 1U) {
        n = cap - 1U;
    }
    memcpy(buf, v->str, n);
    buf[n] = '\0';
    return RS_OK;
}

#else /* !RS_ENABLE_EXPR */

int rs_get_int(rs_t *rs, const char *name, int32_t *out)
{
    (void)rs; (void)name; (void)out;
    return RS_ERR_UNSUPPORTED;
}

int rs_get_float(rs_t *rs, const char *name, float *out)
{
    (void)rs; (void)name; (void)out;
    return RS_ERR_UNSUPPORTED;
}

int rs_get_bool(rs_t *rs, const char *name, bool *out)
{
    (void)rs; (void)name; (void)out;
    return RS_ERR_UNSUPPORTED;
}

int rs_get_str(rs_t *rs, const char *name, char *buf, uint32_t cap)
{
    (void)rs; (void)name; (void)buf; (void)cap;
    return RS_ERR_UNSUPPORTED;
}

#endif /* RS_ENABLE_EXPR */

