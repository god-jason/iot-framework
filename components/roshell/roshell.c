/*
 * roshell.c - roshell 实例生命周期、公开 API 与交互会话
 *
 * create / destroy、脚本加载扫描（rs_scan：块配对校验 + 函数登记）、
 * 同步执行（rs_exec / exec_line）、顶层变量读取与错误 / 程序对象工具；
 * 交互会话（接收缓冲 / 行编辑 / 输入任务 / start / shutdown）也在本文件。
 * 词法 / 求值 / 语句解释分别在 roshell_lexer.c / roshell_eval.c /
 * roshell_interp.c，内置函数表在 roshell_func.c，命令行分派在 roshell_cmd.c。
 */

#include "roshell.h"
#include "roshell_interp.h"
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

/* ============================== 公开 API ============================== */

roshell_t *rs_create(void)
{
    roshell_t *rs = (roshell_t *)ros_mem_calloc(1U, sizeof(*rs));
    if (rs == NULL) {
        return NULL;
    }
    if (vec_init(&rs->top.toks, sizeof(rs_tok_t), RS_TOKEN_MAX) != 0) {
        rs_destroy(rs);
        return NULL;
    }
    return rs;
}

void rs_destroy(roshell_t *rs)
{
    if (rs == NULL) {
        return;
    }
    rs_vars_release(&rs->top);
    vec_deinit(&rs->top.toks);
    vec_deinit(&rs->sleeps);            /* 持续命令状态表（惰性初始化，未用则为空） */
    ros_mem_free(rs);
}

int rs_exec(roshell_t *rs, const char *script, rs_err_t *err)
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
    env.unit = &rs->top;
    env.depth = 0U;
    env.err = err;
    /* 执行帧自持引用：rs_scan 的引用在执行结束后统一释放 */
    prog->refs++;
    rc = rs_exec_unit(&env);
    rs_prog_release(prog);
    return rc;
}

int rs_exec_line(roshell_t *rs, const char *line, rs_err_t *err)
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

void rs_set_cmd_hook(roshell_t *rs, rs_cmd_hook_t hook, void *ctx)
{
    if (rs == NULL) {
        return;
    }
    rs->cmd_hook = hook;
    rs->cmd_hook_ctx = ctx;
}

/* ============================== 顶层变量读取 ============================== */

#if RS_ENABLE_EXPR
static rs_var_t *rs_top_var(roshell_t *rs, const char *name)
{
    if (name[0] == '$') {
        name++;
    }
    return rs_var_find(&rs->top, name, (uint16_t)strlen(name));
}

int rs_get_int(roshell_t *rs, const char *name, int32_t *out)
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

int rs_get_float(roshell_t *rs, const char *name, float *out)
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

int rs_get_bool(roshell_t *rs, const char *name, bool *out)
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

int rs_get_str(roshell_t *rs, const char *name, char *buf, uint32_t cap)
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

int rs_get_int(roshell_t *rs, const char *name, int32_t *out)
{
    (void)rs; (void)name; (void)out;
    return RS_ERR_UNSUPPORTED;
}

int rs_get_float(roshell_t *rs, const char *name, float *out)
{
    (void)rs; (void)name; (void)out;
    return RS_ERR_UNSUPPORTED;
}

int rs_get_bool(roshell_t *rs, const char *name, bool *out)
{
    (void)rs; (void)name; (void)out;
    return RS_ERR_UNSUPPORTED;
}

int rs_get_str(roshell_t *rs, const char *name, char *buf, uint32_t cap)
{
    (void)rs; (void)name; (void)buf; (void)cap;
    return RS_ERR_UNSUPPORTED;
}

#endif /* RS_ENABLE_EXPR */

/* ============================== 交互会话 ============================== */
/*
 * 会话状态直接存放在实例中（见 roshell.h 的 struct roshell）：接收环形缓冲（feed）、
 * 简化行编辑、输入任务（process）与生命周期（start / shutdown）。
 * 命令行分派在 roshell_cmd.c。
 */

#define RS_SHELL_TASK_NAME     "roshell_io"

static void rs_prompt(void)
{
    ros_io_write(NULL, "> ", 2U);
}

/* 处理一个输入字节；返回 true 表示提交了一行 */
static bool rs_edit_byte(roshell_t *rs, uint8_t c)
{
    if (c == 0x03U) {                       /* Ctrl-C：清行 */
        rs->line_len = 0U;
        rs->last_cr = false;
        rs->line[0] = '\0';
        ros_puts("^C");
        rs_prompt();
        return false;
    }
    if (c == '\r' || c == '\n') {
        /* 终端可能只发 CR、只发 LF 或发 CRLF：都按一次提交处理 */
        if (c == '\n' && rs->last_cr) {
            rs->last_cr = false;
            return false;
        }
        rs->last_cr = (c == '\r');
        ros_io_write(NULL, "\n", 1U);
        rs->line[rs->line_len] = '\0';
        if (rs->line_len > 0U) {
            (void)rs_cmd_dispatch(rs, rs->line);
        }
        rs->line_len = 0U;
        if (!rs->exit_req) {
            rs_prompt();
        }
        return true;
    }
    rs->last_cr = false;
    if (c == 0x08U || c == 0x7FU) {         /* 退格 */
        if (rs->line_len > 0U) {
            rs->line_len--;
            rs->line[rs->line_len] = '\0';
            ros_io_write(NULL, "\b \b", 3U);
        }
        return false;
    }
    if (c == 0x1BU) {                       /* ESC：吞掉方向键等控制序列 */
        rs->last_esc = true;
        return false;
    }
    if (rs->last_esc) {
        if (c == '[' || (c >= '0' && c <= '9') || c == ';') {
            return false;
        }
        rs->last_esc = false;
        return false;
    }
    if (c >= 0x20U && c < 0x7FU) {
        if (rs->line_len + 1U < RS_LINE_MAX) {   /* 超长行截断 */
            rs->line[rs->line_len++] = (char)c;
            rs->line[rs->line_len] = '\0';
            ros_putchar((char)c);
        }
    }
    return false;
}

uint32_t rs_shell_process(roshell_t *rs)
{
    uint32_t lines = 0U;
    uint8_t c;

    if (rs == NULL || !rs->started) {
        return 0U;
    }
    /* 推进异步持续命令（'-' 启动），保证即使没有输入也在轮转 on_tick */
    rs_cmd_tick_all();
    /* 先排空本轮全部输入（含 EOF 前已到达的行），再响应退出请求 */
    while (ringbuf_read(&rs->rx, &c, 1U) == 1U) {
        if (rs_edit_byte(rs, c)) {
            lines++;
        }
    }
    if (rs->exit_req) {
        rs->finished = true;        /* 输入任务据此退出 */
    }
    return lines;
}

void rs_shell_feed(roshell_t *rs, const char *data, uint32_t len)
{
    if (rs == NULL || !rs->started || data == NULL || len == 0U) {
        return;
    }
    (void)ringbuf_write(&rs->rx, data, len);
}

static void rs_shell_task_entry(void *arg)
{
    roshell_t *rs = (roshell_t *)arg;

    for (;;) {
        (void)rs_shell_process(rs);
        if (rs->finished) {
            break;
        }
        if (ringbuf_available(&rs->rx) == 0U) {
            /* 有异步持续命令时提高轮转频率，保证 sleep 等命令的精度 */
            ros_sleep_ms(rs_cmd_has_async() ? 1U : 5U);
        }
    }
    ros_task_exit();
}

roshell_t *rs_shell_start(const char *version)
{
    roshell_t *rs;

    rs = rs_create();
    if (rs == NULL) {
        ros_printf("shell: out of memory\r\n");
        return NULL;
    }
    rs_cmd_install(rs);              /* 块内命令路由（规格 §8） */
    (void)ringbuf_init(&rs->rx, rs->rx_mem, sizeof(rs->rx_mem));
    rs->line_len = 0U;
    rs->line[0] = '\0';
    rs->last_cr = false;
    rs->last_esc = false;
    rs->finished = false;
    rs->exit_req = false;
    rs->version = version;
    rs->cwd[0] = '/';
    rs->cwd[1] = '\0';

    rs->task = ros_task_create_dynamic(RS_SHELL_TASK_NAME,
                                       RS_SHELL_PRIO,
                                       rs_shell_task_entry, rs,
                                       RS_SHELL_STACK);
    if (rs->task == NULL) {
        ros_mem_info_t info;

        (void)memset(&info, 0, sizeof(info));
        (void)ros_mem_get_info(&info);
        ros_printf("shell: task create failed (stack=%u, free=%u)\r\n",
                   (unsigned)RS_SHELL_STACK, (unsigned)info.free_bytes);
        rs_destroy(rs);
        return NULL;
    }
    rs->started = true;
    ros_printf("roshell ready (%s): type 'help'\n",
               version != NULL ? version : "pc");
    rs_prompt();
    return rs;
}

bool rs_shell_started(const roshell_t *rs)
{
    return (rs != NULL) && rs->started;
}

/* 当前工作目录；rs 为空或会话未启动时视为根目录 */
const char *rs_shell_cwd(const roshell_t *rs)
{
    return ((rs != NULL) && (rs->cwd[0] != '\0')) ? rs->cwd : "/";
}

/* 保存当前工作目录（调用者需先校验目录存在） */
int rs_shell_cwd_set(roshell_t *rs, const char *path)
{
    size_t len;

    if (rs == NULL || path == NULL || path[0] != '/') {
        return -1;
    }
    len = strlen(path);
    if (len + 1U > sizeof(rs->cwd)) {
        return -1;
    }
    memcpy(rs->cwd, path, len + 1U);
    return 0;
}

bool rs_shell_task_finished(const roshell_t *rs)
{
    return (rs != NULL) && rs->finished;
}

void rs_shell_request_exit(roshell_t *rs)
{
    if (rs != NULL) {
        rs->exit_req = true;
    }
}

int rs_shell_shutdown(roshell_t *rs)
{
    if (rs == NULL || !rs->started || !rs->finished) {
        return -1;
    }
    if (rs->task != NULL) {
        (void)ros_task_destroy(rs->task);
    }
    rs_destroy(rs);
    return 0;
}

uint32_t rs_shell_line_pending(const roshell_t *rs)
{
    return (rs != NULL) ? rs->line_len : 0U;
}

