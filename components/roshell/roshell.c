/*
 * roshell.c - roshell 脚本语言解释器
 *
 * 执行模型（规格 §1.1）：源文本即程序，加载时一次轻量扫描（校验块配对、
 * 登记函数名/起始行/end 行），运行期按行号 PC 就地词法分析并解释执行，
 * 不生成任何指令数组。块嵌套由每执行单元的块帧栈跟踪（≤8 层）。
 *
 * 内存策略（规格 §1.2）：常驻 = 源文本缓冲 + 块帧栈 + 变量表；
 * token 数组按执行单元堆分配、单行复用；脚本执行完即释放文本。
 */

#include "roshell.h"
#include "ros.h"
#include "common.h"

#include <string.h>
#include <stdio.h>

/* ============================== 内部类型 ============================== */

typedef enum {
    RO_VAL_NONE = 0,
    RO_VAL_INT,
    RO_VAL_FLOAT,
    RO_VAL_BOOL,
    RO_VAL_STRING
} ro_val_type_t;

typedef struct {
    uint8_t     type;
    int32_t     i;
    float       f;
    bool        b;
    const char *s;      /* 字符串值：指向变量槽 / 临时缓冲 / run 槽存储 */
    uint16_t    slen;
} ro_value_t;

typedef enum {
    RO_TOK_EOF = 0,
    RO_TOK_WORD,
    RO_TOK_STRING,
    RO_TOK_NUMBER,
    RO_TOK_VAR,
    RO_TOK_OP
} ro_tok_type_t;

typedef struct {
    uint8_t     type;
    const char *p;
    uint16_t    len;
    int32_t     ival;
    float       fval;
    bool        is_float;
} ro_tok_t;

typedef struct {
    char    name[ROSHELL_VAR_NAME_MAX];   /* 不含 $ */
    uint8_t type;
    bool    used;
    int32_t i;
    float   f;
    bool    b;
    char    str[ROSHELL_STR_MAX];
    uint16_t slen;
} ro_var_t;

typedef enum { RO_BLK_IF = 1, RO_BLK_FOR, RO_BLK_WHILE } ro_blk_t;

typedef struct {
    uint8_t  type;        /* ro_blk_t */
    uint16_t start;       /* 块起始行（0 基） */
    uint16_t end;         /* 对应 end 行 */
    uint16_t count;       /* FOR：列表项数 */
    uint16_t idx;         /* FOR：当前迭代下标 */
    bool     taken;       /* IF：本支是否执行 */
} ro_frame_t;

typedef struct {
    char     name[ROSHELL_NAME_MAX];
    uint16_t start;                       /* function 行 */
    uint8_t  argc;
    char     args[ROSHELL_FUNC_ARGS_MAX][ROSHELL_VAR_NAME_MAX];
} ro_func_t;

typedef struct ro_prog {
    char     *text;        /* 换行替换为 \0 */
    uint16_t *line_off;    /* 每行起始偏移 */
    uint16_t *end_of;      /* 块起始行 -> end 行；非块起始为 0 */
    ro_func_t funcs[ROSHELL_FUNC_MAX];
    uint16_t  line_count;
    uint16_t  func_count;
    uint16_t  refs;
} ro_prog_t;

typedef struct {
    volatile uint8_t state;               /* RO_RUN_* */
    int              code;
    roshell_t       *owner;
    ro_prog_t       *prog;                /* 持有一份引用 */
    ros_task_t      *task;
    char             fname[ROSHELL_NAME_MAX]; /* "" 表示执行整脚本 */
    ro_value_t       args[ROSHELL_FUNC_ARGS_MAX];
    char             argstr[ROSHELL_FUNC_ARGS_MAX][ROSHELL_STR_MAX];
    uint8_t          argc;
} ro_slot_t;

#define RO_TMP_SLOTS 4

typedef struct ro_ctx {
    ro_var_t   *vars;                     /* [ROSHELL_VAR_MAX]，堆 */
    bool        own_vars;
    ro_tok_t   *toks;                     /* [ROSHELL_TOKEN_MAX]，堆 */
    ro_frame_t  frames[ROSHELL_BLOCK_DEPTH];
    uint8_t     depth;
    uint16_t    pc;
    bool        in_func;
    bool        returned;
    ro_value_t  ret;
    char        sbuf[RO_TMP_SLOTS][ROSHELL_STR_MAX];
    uint8_t     sflip;
} ro_ctx_t;

struct roshell {
    ro_ctx_t   top;                       /* 持久顶层执行单元 */
    ro_slot_t  slots[ROSHELL_RUN_MAX];
};

typedef struct {
    roshell_t      *rs;
    ro_prog_t      *prog;
    ro_ctx_t       *ctx;
    uint8_t         depth;
    roshell_err_t  *err;
} ro_env_t;

/* ============================== 小工具 ============================== */

static void ro_err(roshell_err_t *e, int code, uint16_t line, const char *msg)
{
    if (e == NULL) {
        return;
    }
    e->code = code;
    e->line = line;
    strncpy(e->msg, msg, sizeof(e->msg) - 1U);
    e->msg[sizeof(e->msg) - 1U] = '\0';
}

static bool ro_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool ro_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool ro_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* 关键字/单词精确匹配（大小写敏感，规格 §2） */
static bool ro_kw(const ro_tok_t *t, const char *s)
{
    uint16_t n;
    if (t == NULL || t->type != RO_TOK_WORD) {
        return false;
    }
    n = (uint16_t)strlen(s);
    return t->len == n && memcmp(t->p, s, n) == 0;
}

/* 运算符 token 匹配 */
static bool ro_op(const ro_tok_t *t, const char *s)
{
    uint16_t n;
    if (t == NULL || t->type != RO_TOK_OP) {
        return false;
    }
    n = (uint16_t)strlen(s);
    return t->len == n && memcmp(t->p, s, n) == 0;
}

/* IEEE754 正无穷（不依赖 math.h，避免常量除零告警） */
static float ro_inf(bool neg)
{
    uint32_t bits = 0x7F800000U;
    float v;
    memcpy(&v, &bits, sizeof(v));
    return neg ? -v : v;
}

/* ============================== 词法 ============================== */

static int ro_lex(const char *text, ro_tok_t *toks, uint16_t max)
{
    uint16_t i = 0U;
    uint16_t n = 0U;
    if (text == NULL || toks == NULL || max < 2U) {
        return 0;
    }
    while (text[i] != '\0' && n + 1U < max) {
        ro_tok_t *t = &toks[n];
        char c = text[i];
        if (ro_is_space(c)) {
            i++;
            continue;
        }
        if (c == ';') {                     /* 注释：token 边界，到行尾 */
            break;
        }
        if (c == '/' && text[i + 1U] == '/') {
            break;
        }
        memset(t, 0, sizeof(*t));
        t->p = &text[i];
        if (c == '"') {                     /* 字符串：含引号保存，求值去转义 */
            uint16_t quote = i;
            i++;
            while (text[i] != '\0') {
                if (text[i] == '\\' && text[i + 1U] == '"') {
                    i = (uint16_t)(i + 2U);
                    continue;
                }
                if (text[i] == '"') {
                    break;
                }
                i++;
            }
            t->type = RO_TOK_STRING;
            t->p = &text[quote];
            if (text[i] == '"') {
                t->len = (uint16_t)(i - quote + 1U);
                i++;
            } else {
                t->len = 1U;                /* 未闭合：只留开引号，求值报错 */
            }
            n++;
            continue;
        }
        if (c == '$') {
            uint16_t start = i;
            i++;
            while (ro_is_alpha(text[i]) || ro_is_digit(text[i])) {
                i++;
            }
            t->type = RO_TOK_VAR;
            t->p = &text[start];
            t->len = (uint16_t)(i - start);
            n++;
            continue;
        }
        if (ro_is_digit(c) || (c == '.' && ro_is_digit(text[i + 1U]))) {
            uint16_t start = i;
            bool is_float = false;
            while (ro_is_digit(text[i])) {
                i++;
            }
            if (text[i] == '.') {
                is_float = true;
                i++;
                while (ro_is_digit(text[i])) {
                    i++;
                }
            }
            if (text[i] == 'e' || text[i] == 'E') {
                uint16_t save = i;
                i++;
                if (text[i] == '+' || text[i] == '-') {
                    i++;
                }
                if (ro_is_digit(text[i])) {
                    is_float = true;
                    while (ro_is_digit(text[i])) {
                        i++;
                    }
                } else {
                    i = save;               /* 不是指数，回退 */
                }
            }
            t->type = RO_TOK_NUMBER;
            t->p = &text[start];
            t->len = (uint16_t)(i - start);
            /* 手工数值转换（裸机可用，不依赖 strtof） */
            {
                uint32_t ip = 0U;
                float frac = 0.0f;
                float scale = 0.1f;
                bool dot = false;
                uint16_t k;
                for (k = 0U; k < t->len; k++) {
                    char d = t->p[k];
                    if (d == '.') {
                        dot = true;
                    } else if (d == 'e' || d == 'E') {
                        break;
                    } else if (ro_is_digit(d)) {
                        if (dot) {
                            frac += (float)(d - '0') * scale;
                            scale *= 0.1f;
                        } else {
                            ip = ip * 10U + (uint32_t)(d - '0');
                        }
                    }
                }
                t->ival = (int32_t)ip;
                t->fval = (float)ip + frac;
                t->is_float = is_float;
                for (k = 0U; k < t->len; k++) {
                    if (t->p[k] == 'e' || t->p[k] == 'E') {
                        int32_t exp = 0;
                        bool eneg = false;
                        float escale = 1.0f;
                        k++;
                        if (k < t->len && (t->p[k] == '+' || t->p[k] == '-')) {
                            eneg = (t->p[k] == '-');
                            k++;
                        }
                        while (k < t->len && ro_is_digit(t->p[k])) {
                            exp = exp * 10 + (t->p[k] - '0');
                            k++;
                        }
                        while (exp-- > 0) {
                            escale *= 10.0f;
                        }
                        t->fval = eneg ? (t->fval / escale) : (t->fval * escale);
                        break;
                    }
                }
            }
            n++;
            continue;
        }
        if (ro_is_alpha(c)) {
            uint16_t start = i;
            while (ro_is_alpha(text[i]) || ro_is_digit(text[i])) {
                i++;
            }
            t->type = RO_TOK_WORD;
            t->p = &text[start];
            t->len = (uint16_t)(i - start);
            n++;
            continue;
        }
        /* 运算符：双字符优先（含 ++ --）；逗号作为独立 token（for/call 分隔） */
        t->type = RO_TOK_OP;
        {
            char c2 = text[i + 1U];
            if ((c == '=' && c2 == '=') || (c == '!' && c2 == '=') ||
                (c == '<' && c2 == '=') || (c == '>' && c2 == '=') ||
                (c == '&' && c2 == '&') || (c == '|' && c2 == '|') ||
                (c == '+' && c2 == '+') || (c == '-' && c2 == '-')) {
                t->len = 2U;
                i = (uint16_t)(i + 2U);
                n++;
                continue;
            }
        }
        t->len = 1U;
        i++;
        n++;
    }
    memset(&toks[n], 0, sizeof(toks[n]));
    toks[n].type = RO_TOK_EOF;
    return (int)(n + 1U);
}

/* 取字符串 token 的去转义内容；未闭合返回 false */
static bool ro_str_unescape(const ro_tok_t *t, char *dst, uint16_t cap, uint16_t *out_len)
{
    uint16_t i = 1U;
    uint16_t o = 0U;
    if (t->type != RO_TOK_STRING || t->len < 2U) {
        return false;
    }
    while (i + 1U < t->len) {              /* 末字符是右引号 */
        char c = t->p[i];
        if (c == '\\' && t->p[i + 1U] == '"') {
            c = '"';
            i++;
        }
        if (o + 1U >= cap) {
            return false;
        }
        dst[o++] = c;
        i++;
    }
    dst[o] = '\0';
    *out_len = o;
    return true;
}

/* ============================== 行首关键字分类 ============================== */

enum {
    K_NONE = 0, K_IF, K_ELSEIF, K_ELSE, K_END, K_FOR, K_WHILE,
    K_FUNCTION, K_BREAK, K_RETURN, K_CALL, K_RUN, K_WAIT, K_LOAD
};

static const char *const ro_kws[] = {
    "if", "elseif", "else", "end", "for", "while",
    "function", "break", "return", "call", "run", "wait", "load"
};

/* 只识别行首关键字（大小写敏感），用于扫描与 if 链深度扫描 */
static int ro_line_kind(const char *line)
{
    char buf[ROSHELL_NAME_MAX];
    uint16_t i = 0U;
    uint16_t n = 0U;
    uint32_t k;
    while (ro_is_space(line[i])) {
        i++;
    }
    if (!ro_is_alpha(line[i])) {
        return K_NONE;
    }
    while (ro_is_alpha(line[i]) || ro_is_digit(line[i])) {
        if (n + 1U < sizeof(buf)) {
            buf[n++] = line[i];
        } else {
            return K_NONE;
        }
        i++;
    }
    buf[n] = '\0';
    for (k = 0U; k < sizeof(ro_kws) / sizeof(ro_kws[0]); k++) {
        if (strcmp(buf, ro_kws[k]) == 0) {
            return (int)(K_IF + k);
        }
    }
    return K_NONE;
}

/* ============================== 值辅助（EXPR） ============================== */

#if ROSHELL_ENABLE_EXPR

static ro_var_t *ro_var_find(ro_ctx_t *ctx, const char *name, uint16_t len)
{
    uint32_t i;
    if (len == 0U || len >= ROSHELL_VAR_NAME_MAX) {
        return NULL;
    }
    for (i = 0U; i < ROSHELL_VAR_MAX; i++) {
        ro_var_t *v = &ctx->vars[i];
        if (v->used && strlen(v->name) == (size_t)len &&
            memcmp(v->name, name, len) == 0) {
            return v;
        }
    }
    return NULL;
}

static ro_var_t *ro_var_alloc(ro_ctx_t *ctx, const char *name, uint16_t len)
{
    ro_var_t *v;
    uint32_t i;
    if (len == 0U || len >= ROSHELL_VAR_NAME_MAX) {
        return NULL;
    }
    v = ro_var_find(ctx, name, len);
    if (v != NULL) {
        return v;
    }
    for (i = 0U; i < ROSHELL_VAR_MAX; i++) {
        if (!ctx->vars[i].used) {
            v = &ctx->vars[i];
            break;
        }
    }
    if (i >= ROSHELL_VAR_MAX) {
        return NULL;                        /* 变量表满 */
    }
    memset(v, 0, sizeof(*v));
    memcpy(v->name, name, len);
    v->name[len] = '\0';
    v->used = true;
    return v;
}

static void ro_var_store(ro_var_t *v, const ro_value_t *val)
{
    v->type = val->type;
    switch (val->type) {
    case RO_VAL_INT:   v->i = val->i; break;
    case RO_VAL_FLOAT: v->f = val->f; break;
    case RO_VAL_BOOL:  v->b = val->b; break;
    default:
        memcpy(v->str, val->s, val->slen);
        v->str[val->slen] = '\0';
        v->slen = val->slen;
        break;
    }
}

/* 临时字符串槽（ping-pong，供字面量 / 拼接 / 返回值拷贝） */
static const char *ro_tmp(ro_ctx_t *ctx, const char *src, uint16_t len,
                          uint16_t *out_len)
{
    char *buf = ctx->sbuf[ctx->sflip];
    ctx->sflip = (uint8_t)((ctx->sflip + 1U) % RO_TMP_SLOTS);
    if (len >= ROSHELL_STR_MAX) {
        len = ROSHELL_STR_MAX - 1U;
    }
    memcpy(buf, src, len);
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

static bool ro_val_is_num(const ro_value_t *v)
{
    return v->type == RO_VAL_INT || v->type == RO_VAL_FLOAT;
}

static float ro_val_fnum(const ro_value_t *v)
{
    return v->type == RO_VAL_FLOAT ? v->f : (float)v->i;
}

static uint16_t ro_fmt_int(int32_t v, char *buf, uint16_t cap)
{
    char tmp[12];
    uint16_t k = 0U;
    uint16_t o = 0U;
    uint32_t u;
    if (v < 0) {
        buf[o++] = '-';
        u = (uint32_t)(-(int64_t)v);
    } else {
        u = (uint32_t)v;
    }
    if (u == 0U) {
        tmp[k++] = '0';
    }
    while (u > 0U) {
        tmp[k++] = (char)('0' + (int)(u % 10U));
        u /= 10U;
    }
    while (k > 0U && o + 1U < cap) {
        buf[o++] = tmp[--k];
    }
    buf[o] = '\0';
    return o;
}

/* 值 -> 字符串形式（拼接与字符串值格式化用） */
static uint16_t ro_fmt_value(const ro_value_t *v, char *buf, uint16_t cap)
{
    uint16_t o = 0U;
    buf[0] = '\0';
    switch (v->type) {
    case RO_VAL_BOOL: {
        const char *s = v->b ? "true" : "false";
        uint16_t n = (uint16_t)strlen(s);
        if (n >= cap) {
            n = (uint16_t)(cap - 1U);
        }
        memcpy(buf, s, n);
        buf[n] = '\0';
        return n;
    }
    case RO_VAL_INT:
        return ro_fmt_int(v->i, buf, cap);
    case RO_VAL_FLOAT: {
        float f = v->f;
        int32_t ip;
        if (f != f) {
            strncpy(buf, "nan", cap);
            buf[cap - 1U] = '\0';
            return (uint16_t)strlen(buf);
        }
        if (f == ro_inf(false)) {
            strncpy(buf, "inf", cap);
            buf[cap - 1U] = '\0';
            return 3U;
        }
        if (f == ro_inf(true)) {
            strncpy(buf, "-inf", cap);
            buf[cap - 1U] = '\0';
            return 4U;
        }
        if (f < 0.0f) {
            buf[o++] = '-';
            f = -f;
        }
        ip = (int32_t)f;
        if ((float)ip == f) {
            o += ro_fmt_int(ip, buf + o, (uint16_t)(cap - o));
            return o;
        }
        o += ro_fmt_int(ip, buf + o, (uint16_t)(cap - o));
        if (o + 1U < cap) {
            buf[o++] = '.';
        }
        {
            float fr = f - (float)ip;
            int d;
            for (d = 0; d < 4 && o + 1U < cap; d++) {
                int fd;
                fr *= 10.0f;
                fd = (int)fr;
                fr -= (float)fd;
                buf[o++] = (char)('0' + fd);
            }
            while (o > 0U && buf[o - 1U] == '0') {
                o--;
            }
            if (o > 0U && buf[o - 1U] == '.') {
                o--;
            }
        }
        buf[o] = '\0';
        return o;
    }
    default: {                              /* STRING */
        uint16_t n = v->slen;
        if (n >= cap) {
            n = (uint16_t)(cap - 1U);
        }
        memcpy(buf, v->s, n);
        buf[n] = '\0';
        return n;
    }
    }
}

#endif /* ROSHELL_ENABLE_EXPR */

/* ============================== 表达式解析（EXPR） ============================== */

#if ROSHELL_ENABLE_EXPR

typedef struct {
    const ro_tok_t *t;
    int      n;
    int      pos;
    bool     noeval;   /* 短路：跳过求值但仍消费 token */
} ro_ps_t;

static int ro_p_expr(ro_env_t *env, ro_ps_t *ps, ro_value_t *out);
static int ro_p_args(ro_env_t *env, ro_ps_t *ps, ro_value_t *args, uint8_t *argc);

static void ro_err0(roshell_err_t *e, int code, const char *msg);
static const ro_func_t *ro_find_func(const ro_prog_t *p, const char *name, uint16_t len);
static int ro_call_body(roshell_t *rs, ro_prog_t *prog, const ro_func_t *f,
                        const ro_value_t *args, uint8_t depth,
                        ro_ctx_t *caller, ro_value_t *out, roshell_err_t *err);

static int ro_type_err(ro_env_t *env, const char *msg)
{
    ro_err0(env->err, RO_ERR_TYPE, msg);
    return RO_ERR_TYPE;
}

static int ro_parse_err(ro_env_t *env, const char *msg)
{
    ro_err0(env->err, RO_ERR_PARSE, msg);
    return RO_ERR_PARSE;
}

/* 数值比较：-1 / 0 / 1 */
static void ro_cmp_num(const ro_value_t *a, const ro_value_t *b, int *res)
{
    if (a->type == RO_VAL_INT && b->type == RO_VAL_INT) {
        *res = (a->i < b->i) ? -1 : ((a->i > b->i) ? 1 : 0);
    } else {
        float x = ro_val_fnum(a);
        float y = ro_val_fnum(b);
        *res = (x < y) ? -1 : ((x > y) ? 1 : 0);
    }
}

static int ro_binop(ro_env_t *env, const ro_tok_t *op, const ro_value_t *a,
                    const ro_value_t *b, ro_value_t *out)
{
    char c0 = op->p[0];
    ro_value_t va;
    ro_value_t vb;
    /* 调用方常以 ro_binop(op, out, &b, out) 传参：先快照再清零 out */
    va = *a;
    vb = *b;
    a = &va;
    b = &vb;
    memset(out, 0, sizeof(*out));
    if (op->len == 2U) {
        if (c0 == '&' || c0 == '|') {
            if (a->type != RO_VAL_BOOL || b->type != RO_VAL_BOOL) {
                return ro_type_err(env, "logical operands must be boolean");
            }
            out->type = RO_VAL_BOOL;
            out->b = (c0 == '&') ? (a->b && b->b) : (a->b || b->b);
            return RO_OK;
        }
        if (c0 == '+' || c0 == '-') {
            return ro_parse_err(env, "unknown statement");
        }
        /* == != <= >= */
        if (c0 == '=' || c0 == '!') {
            bool eq;
            if (a->type == RO_VAL_STRING || b->type == RO_VAL_STRING) {
                eq = (a->type == RO_VAL_STRING && b->type == RO_VAL_STRING &&
                      a->slen == b->slen &&
                      memcmp(a->s, b->s, a->slen) == 0);
            } else if (a->type == RO_VAL_BOOL || b->type == RO_VAL_BOOL) {
                if (a->type != RO_VAL_BOOL || b->type != RO_VAL_BOOL) {
                    return ro_type_err(env, "type mismatch");
                }
                eq = (a->b == b->b);
            } else {
                int r;
                ro_cmp_num(a, b, &r);
                eq = (r == 0);
            }
            out->type = RO_VAL_BOOL;
            out->b = (c0 == '=') ? eq : !eq;
            return RO_OK;
        }
        /* <= >= */
        {
            int r;
            if (!ro_val_is_num(a) || !ro_val_is_num(b)) {
                return ro_type_err(env, "comparison needs numbers");
            }
            ro_cmp_num(a, b, &r);
            out->type = RO_VAL_BOOL;
            out->b = (c0 == '<') ? (r <= 0) : (r >= 0);
            return RO_OK;
        }
    }
    switch (c0) {
    case '<': case '>': {
        int r;
        if (!ro_val_is_num(a) || !ro_val_is_num(b)) {
            return ro_type_err(env, "comparison needs numbers");
        }
        ro_cmp_num(a, b, &r);
        out->type = RO_VAL_BOOL;
        out->b = (c0 == '<') ? (r < 0) : (r > 0);
        return RO_OK;
    }
    case '+':
        if (a->type == RO_VAL_STRING || b->type == RO_VAL_STRING) {
            char fa[ROSHELL_STR_MAX];
            char fb[ROSHELL_STR_MAX];
            char cat[ROSHELL_STR_MAX * 2];
            uint16_t la = ro_fmt_value(a, fa, sizeof(fa));
            uint16_t lb = ro_fmt_value(b, fb, sizeof(fb));
            uint16_t len;
            if ((uint32_t)la + lb >= ROSHELL_STR_MAX) {
                ro_err0(env->err, RO_ERR_LIMIT, "string too long");
                return RO_ERR_LIMIT;
            }
            memcpy(cat, fa, la);
            memcpy(cat + la, fb, lb);
            len = (uint16_t)(la + lb);
            out->type = RO_VAL_STRING;
            out->s = ro_tmp(env->ctx, cat, len, &out->slen);
            return RO_OK;
        }
        if (!ro_val_is_num(a) || !ro_val_is_num(b)) {
            return ro_type_err(env, "type mismatch");
        }
        break;
    case '-':
        if (!ro_val_is_num(a) || !ro_val_is_num(b)) {
            return ro_type_err(env, "type mismatch");
        }
        break;
    case '*': case '/':
        if (!ro_val_is_num(a) || !ro_val_is_num(b)) {
            return ro_type_err(env, "type mismatch");
        }
        break;
    case '%':
        if (a->type != RO_VAL_INT || b->type != RO_VAL_INT) {
            return ro_type_err(env, "type mismatch");
        }
        if (b->i == 0) {
            ro_err0(env->err, RO_ERR_DIV_ZERO, "modulo by zero");
            return RO_ERR_DIV_ZERO;
        }
        out->type = RO_VAL_INT;
        out->i = a->i % b->i;
        return RO_OK;
    default:
        return ro_parse_err(env, "unknown statement");
    }
    /* 加 / 减 */
    if (a->type == RO_VAL_INT && b->type == RO_VAL_INT) {
        out->type = RO_VAL_INT;
        out->i = (c0 == '+') ? (a->i + b->i) : (a->i - b->i);
        return RO_OK;
    }
    out->type = RO_VAL_FLOAT;
    out->f = (c0 == '+') ? (ro_val_fnum(a) + ro_val_fnum(b))
                         : (ro_val_fnum(a) - ro_val_fnum(b));
    return RO_OK;
}

/* 乘 / 除（/ 单独处理：整除与除零 inf） */
static int ro_mulop(ro_env_t *env, const ro_tok_t *op, const ro_value_t *a,
                    const ro_value_t *b, ro_value_t *out)
{
    char c0 = op->p[0];
    ro_value_t va;
    ro_value_t vb;
    if (c0 == '%') {
        return ro_binop(env, op, a, b, out);
    }
    if (!ro_val_is_num(a) || !ro_val_is_num(b)) {
        return ro_type_err(env, "type mismatch");
    }
    /* 调用方常以 ro_mulop(op, out, &b, out) 传参：先快照再清零 out */
    va = *a;
    vb = *b;
    a = &va;
    b = &vb;
    memset(out, 0, sizeof(*out));
    if (c0 == '*') {
        if (a->type == RO_VAL_INT && b->type == RO_VAL_INT) {
            out->type = RO_VAL_INT;
            out->i = a->i * b->i;
        } else {
            out->type = RO_VAL_FLOAT;
            out->f = ro_val_fnum(a) * ro_val_fnum(b);
        }
        return RO_OK;
    }
    /* '/'：除零 -> 浮点 ±inf（规格 §3.2） */
    if (a->type == RO_VAL_INT && b->type == RO_VAL_INT) {
        if (b->i == 0) {
            out->type = RO_VAL_FLOAT;
            out->f = ro_inf(a->i < 0);
            return RO_OK;
        }
        out->type = RO_VAL_INT;
        out->i = a->i / b->i;
        return RO_OK;
    }
    if (ro_val_fnum(b) == 0.0f) {
        out->type = RO_VAL_FLOAT;
        out->f = ro_inf(ro_val_fnum(a) < 0.0f);
        return RO_OK;
    }
    out->type = RO_VAL_FLOAT;
    out->f = ro_val_fnum(a) / ro_val_fnum(b);
    return RO_OK;
}

static int ro_p_primary(ro_env_t *env, ro_ps_t *ps, ro_value_t *out)
{
    const ro_tok_t *t = &ps->t[ps->pos];
    memset(out, 0, sizeof(*out));
    switch (t->type) {
    case RO_TOK_NUMBER:
        ps->pos++;
        if (t->is_float) {
            out->type = RO_VAL_FLOAT;
            out->f = t->fval;
        } else {
            out->type = RO_VAL_INT;
            out->i = t->ival;
        }
        return RO_OK;
    case RO_TOK_STRING: {
        char sb[ROSHELL_STR_MAX];
        uint16_t len;
        ps->pos++;
        if (ps->noeval) {
            out->type = RO_VAL_INT;
            return RO_OK;
        }
        if (t->len > ROSHELL_STR_MAX + 1U) {
            return ro_parse_err(env, "string too long");
        }
        if (!ro_str_unescape(t, sb, sizeof(sb), &len)) {
            return ro_parse_err(env, "unclosed string");
        }
        out->type = RO_VAL_STRING;
        out->s = ro_tmp(env->ctx, sb, len, &out->slen);
        return RO_OK;
    }
    case RO_TOK_VAR: {
        ro_var_t *v;
        ps->pos++;
        if (ps->noeval) {
            out->type = RO_VAL_INT;
        } else {
            v = ro_var_find(env->ctx, t->p + 1, (uint16_t)(t->len - 1U));
            if (v == NULL) {
                ro_err0(env->err, RO_ERR_UNDEF_VAR, "undefined variable");
                return RO_ERR_UNDEF_VAR;
            }
            out->type = v->type;
            out->i = v->i;
            out->f = v->f;
            out->b = v->b;
            out->s = v->str;
            out->slen = v->slen;
        }
        /* 后缀 ++ / --：先取值后加减 */
        if (ro_op(&ps->t[ps->pos], "++") || ro_op(&ps->t[ps->pos], "--")) {
            bool inc = (ps->t[ps->pos].p[0] == '+');
            ps->pos++;
            if (!ps->noeval) {
                if (out->type != RO_VAL_INT && out->type != RO_VAL_FLOAT) {
                    return ro_type_err(env, "type mismatch");
                }
                v = ro_var_find(env->ctx, t->p + 1, (uint16_t)(t->len - 1U));
                if (v == NULL) {
                    ro_err0(env->err, RO_ERR_UNDEF_VAR, "undefined variable");
                    return RO_ERR_UNDEF_VAR;
                }
                if (v->type == RO_VAL_INT) {
                    v->i += inc ? 1 : -1;
                } else {
                    v->f += inc ? 1.0f : -1.0f;
                }
            }
        }
        return RO_OK;
    }
    case RO_TOK_WORD:
        if (ro_kw(t, "true") || ro_kw(t, "false")) {
            out->type = RO_VAL_BOOL;
            out->b = ro_kw(t, "true");
            ps->pos++;
            return RO_OK;
        }
        if (ro_op(&ps->t[ps->pos + 1], "(")) {
            ro_value_t args[ROSHELL_FUNC_ARGS_MAX];
            uint8_t argc = 0;
            const ro_func_t *f;
            int rc;
            f = ro_find_func(env->prog, t->p, t->len);
            ps->pos++;
            rc = ro_p_args(env, ps, args, &argc);
            if (rc != RO_OK) {
                return rc;
            }
            if (ps->noeval) {
                out->type = RO_VAL_INT;
                return RO_OK;
            }
            if (f == NULL) {
                ro_err0(env->err, RO_ERR_UNDEF_FUNC, "undefined function");
                return RO_ERR_UNDEF_FUNC;
            }
            if (argc != f->argc) {
                ro_err0(env->err, RO_ERR_ARG_COUNT, "arg count mismatch");
                return RO_ERR_ARG_COUNT;
            }
            if ((uint32_t)env->depth + 1U > ROSHELL_CALL_DEPTH) {
                ro_err0(env->err, RO_ERR_STACK_OVERFLOW, "call stack overflow");
                return RO_ERR_STACK_OVERFLOW;
            }
            return ro_call_body(env->rs, env->prog, f, args,
                                (uint8_t)(env->depth + 1U), env->ctx, out,
                                env->err);
        }
        return ro_parse_err(env, "unknown statement");
    case RO_TOK_OP:
        if (ro_op(t, "(")) {
            int rc;
            ps->pos++;
            rc = ro_p_expr(env, ps, out);
            if (rc != RO_OK) {
                return rc;
            }
            if (!ro_op(&ps->t[ps->pos], ")")) {
                return ro_parse_err(env, "missing )");
            }
            ps->pos++;
            return RO_OK;
        }
        return ro_parse_err(env, "unknown statement");
    default:
        return ro_parse_err(env, "unknown statement");
    }
}

static int ro_p_unary(ro_env_t *env, ro_ps_t *ps, ro_value_t *out)
{
    const ro_tok_t *t = &ps->t[ps->pos];
    if (ro_op(t, "!")) {
        int rc;
        ps->pos++;
        rc = ro_p_unary(env, ps, out);
        if (rc != RO_OK) {
            return rc;
        }
        if (ps->noeval) {
            return RO_OK;
        }
        if (out->type != RO_VAL_BOOL) {
            return ro_type_err(env, "! needs boolean");
        }
        out->b = !out->b;
        return RO_OK;
    }
    if (ro_op(t, "-") && t->len == 1U) {
        int rc;
        ps->pos++;
        rc = ro_p_unary(env, ps, out);
        if (rc != RO_OK) {
            return rc;
        }
        if (ps->noeval) {
            return RO_OK;
        }
        if (!ro_val_is_num(out)) {
            return ro_type_err(env, "type mismatch");
        }
        if (out->type == RO_VAL_INT) {
            out->i = -out->i;
        } else {
            out->f = -out->f;
        }
        return RO_OK;
    }
    if (ro_op(t, "++") || ro_op(t, "--")) {
        bool inc = (t->p[0] == '+');
        const ro_tok_t *vt;
        ro_var_t *v;
        ps->pos++;
        vt = &ps->t[ps->pos];
        if (vt->type != RO_TOK_VAR) {
            return ro_parse_err(env, "++/-- needs variable");
        }
        ps->pos++;
        if (ps->noeval) {
            memset(out, 0, sizeof(*out));
            out->type = RO_VAL_INT;
            return RO_OK;
        }
        v = ro_var_find(env->ctx, vt->p + 1, (uint16_t)(vt->len - 1U));
        if (v == NULL) {
            ro_err0(env->err, RO_ERR_UNDEF_VAR, "undefined variable");
            return RO_ERR_UNDEF_VAR;
        }
        if (v->type != RO_VAL_INT && v->type != RO_VAL_FLOAT) {
            return ro_type_err(env, "type mismatch");
        }
        if (v->type == RO_VAL_INT) {
            v->i += inc ? 1 : -1;
            out->type = RO_VAL_INT;
            out->i = v->i;
        } else {
            v->f += inc ? 1.0f : -1.0f;
            out->type = RO_VAL_FLOAT;
            out->f = v->f;
        }
        return RO_OK;
    }
    return ro_p_primary(env, ps, out);
}

static int ro_p_mul(ro_env_t *env, ro_ps_t *ps, ro_value_t *out)
{
    ro_value_t b;
    int rc = ro_p_unary(env, ps, out);
    while (rc == RO_OK && ps->t[ps->pos].type == RO_TOK_OP &&
           (ps->t[ps->pos].p[0] == '*' || ps->t[ps->pos].p[0] == '/' ||
            ps->t[ps->pos].p[0] == '%') && ps->t[ps->pos].len == 1U) {
        const ro_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        rc = ro_p_unary(env, ps, &b);
        if (rc != RO_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = ro_mulop(env, op, out, &b, out);
    }
    return rc;
}

static int ro_p_add(ro_env_t *env, ro_ps_t *ps, ro_value_t *out)
{
    ro_value_t b;
    int rc = ro_p_mul(env, ps, out);
    while (rc == RO_OK && ps->t[ps->pos].type == RO_TOK_OP &&
           (ps->t[ps->pos].p[0] == '+' || ps->t[ps->pos].p[0] == '-') &&
           ps->t[ps->pos].len == 1U) {
        const ro_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        rc = ro_p_mul(env, ps, &b);
        if (rc != RO_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = ro_binop(env, op, out, &b, out);
    }
    return rc;
}

static int ro_p_rel(ro_env_t *env, ro_ps_t *ps, ro_value_t *out)
{
    ro_value_t b;
    int rc = ro_p_add(env, ps, out);
    /* < <= > >=（规格 §3.2 关系运算） */
    while (rc == RO_OK && ps->t[ps->pos].type == RO_TOK_OP &&
           (ps->t[ps->pos].p[0] == '<' || ps->t[ps->pos].p[0] == '>') &&
           ps->t[ps->pos].len <= 2U) {
        const ro_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        rc = ro_p_add(env, ps, &b);
        if (rc != RO_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = ro_binop(env, op, out, &b, out);
    }
    return rc;
}

static int ro_p_eq(ro_env_t *env, ro_ps_t *ps, ro_value_t *out)
{
    ro_value_t b;
    int rc = ro_p_rel(env, ps, out);
    while (rc == RO_OK && ps->t[ps->pos].type == RO_TOK_OP &&
           (ps->t[ps->pos].p[0] == '=' || ps->t[ps->pos].p[0] == '!') &&
           ps->t[ps->pos].len == 2U) {
        const ro_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        rc = ro_p_rel(env, ps, &b);
        if (rc != RO_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = ro_binop(env, op, out, &b, out);
    }
    return rc;
}

static int ro_p_and(ro_env_t *env, ro_ps_t *ps, ro_value_t *out)
{
    ro_value_t b;
    int rc = ro_p_eq(env, ps, out);
    while (rc == RO_OK && ro_op(&ps->t[ps->pos], "&&")) {
        const ro_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        if (!ps->noeval && out->type == RO_VAL_BOOL && !out->b) {
            ro_value_t dummy;
            ps->noeval = true;
            rc = ro_p_eq(env, ps, &dummy);
            ps->noeval = false;
            if (rc != RO_OK) {
                return rc;
            }
            continue;                       /* 短路：保持 false */
        }
        rc = ro_p_eq(env, ps, &b);
        if (rc != RO_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = ro_binop(env, op, out, &b, out);
    }
    return rc;
}

static int ro_p_or(ro_env_t *env, ro_ps_t *ps, ro_value_t *out)
{
    ro_value_t b;
    int rc = ro_p_and(env, ps, out);
    while (rc == RO_OK && ro_op(&ps->t[ps->pos], "||")) {
        const ro_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        if (!ps->noeval && out->type == RO_VAL_BOOL && out->b) {
            ro_value_t dummy;
            ps->noeval = true;
            rc = ro_p_and(env, ps, &dummy);
            ps->noeval = false;
            if (rc != RO_OK) {
                return rc;
            }
            continue;
        }
        rc = ro_p_and(env, ps, &b);
        if (rc != RO_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = ro_binop(env, op, out, &b, out);
    }
    return rc;
}

static int ro_p_expr(ro_env_t *env, ro_ps_t *ps, ro_value_t *out)
{
    return ro_p_or(env, ps, out);
}

/* 实参列表：ps->pos 指向 '(' */
static int ro_p_args(ro_env_t *env, ro_ps_t *ps, ro_value_t *args, uint8_t *argc)
{
    int rc;
    *argc = 0;
    ps->pos++;
    if (ro_op(&ps->t[ps->pos], ")")) {
        ps->pos++;
        return RO_OK;
    }
    for (;;) {
        if (*argc >= ROSHELL_FUNC_ARGS_MAX) {
            return ro_err0(env->err, RO_ERR_LIMIT, "too many arguments"),
                   RO_ERR_LIMIT;
        }
        rc = ro_p_expr(env, ps, &args[*argc]);
        if (rc != RO_OK) {
            return rc;
        }
        (*argc)++;
        if (ro_op(&ps->t[ps->pos], ",")) {
            ps->pos++;
            continue;
        }
        if (ro_op(&ps->t[ps->pos], ")")) {
            ps->pos++;
            return RO_OK;
        }
        return ro_parse_err(env, "bad argument list");
    }
}

/* 求值 toks[start..] 并要求全部消费 */
static int ro_eval_tokens(ro_env_t *env, const ro_tok_t *toks, int n, int start,
                          ro_value_t *out)
{
    ro_ps_t ps;
    int rc;
    ps.t = toks;
    ps.n = n;
    ps.pos = start;
    ps.noeval = false;
    rc = ro_p_expr(env, &ps, out);
    if (rc != RO_OK) {
        return rc;
    }
    if (ps.pos != n - 1) {
        return ro_parse_err(env, "syntax error");
    }
    return RO_OK;
}

static int ro_eval_cond(ro_env_t *env, const ro_tok_t *t, int tn, int start,
                        bool *out)
{
    ro_value_t v;
    int rc = ro_eval_tokens(env, t, tn, start, &v);
    if (rc != RO_OK) {
        return rc;
    }
    if (v.type != RO_VAL_BOOL) {
        return ro_type_err(env, "condition must be boolean");
    }
    *out = v.b;
    return RO_OK;
}

#endif /* ROSHELL_ENABLE_EXPR */

/* ============================== 错误 / 程序对象 ============================== */

static void ro_err0(roshell_err_t *e, int code, const char *msg)
{
    if (e == NULL) {
        return;
    }
    e->code = code;
    strncpy(e->msg, msg, sizeof(e->msg) - 1U);
    e->msg[sizeof(e->msg) - 1U] = '\0';
}

static void ro_prog_free(ro_prog_t *p)
{
    if (p == NULL) {
        return;
    }
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

static void ro_prog_release(ro_prog_t *p)
{
    if (p != NULL && --p->refs == 0U) {
        ro_prog_free(p);
    }
}

static const ro_func_t *ro_find_func(const ro_prog_t *p, const char *name, uint16_t len)
{
    uint16_t i;
    for (i = 0U; i < p->func_count; i++) {
        if (strlen(p->funcs[i].name) == (size_t)len &&
            memcmp(p->funcs[i].name, name, len) == 0) {
            return &p->funcs[i];
        }
    }
    return NULL;
}

/* ============================== 加载扫描（轻量校验 + 函数登记） ============================== */

typedef struct {
    int      kind;
    uint16_t line;
    bool     has_else;
} ro_pend_t;

static ro_prog_t *ro_scan(const char *script, roshell_err_t *err)
{
    ro_prog_t *p;
    ro_tok_t *toks;
    ro_pend_t pend[ROSHELL_BLOCK_DEPTH];
    uint8_t depth = 0U;
    size_t len;
    size_t i;
    uint16_t nlines = 0U;
    uint16_t k;
    len = strlen(script);
    if (len > ROSHELL_SCRIPT_MAX) {
        ro_err(err, RO_ERR_LIMIT, 0U, "script too long");
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
    p = (ro_prog_t *)ros_mem_calloc(1U, sizeof(*p));
    toks = (ro_tok_t *)ros_mem_malloc(ROSHELL_TOKEN_MAX * sizeof(ro_tok_t));
    if (p != NULL && toks != NULL) {
        p->text = (char *)ros_mem_malloc(len + 1U);
        p->line_off = (uint16_t *)ros_mem_malloc((size_t)(nlines + 1U) * sizeof(uint16_t));
        p->end_of = (uint16_t *)ros_mem_calloc((size_t)(nlines + 1U), sizeof(uint16_t));
    }
    if (p == NULL || toks == NULL || p->text == NULL ||
        p->line_off == NULL || p->end_of == NULL) {
        ro_err(err, RO_ERR_RESOURCE, 0U, "out of memory");
        if (toks != NULL) {
            ros_mem_free(toks);
        }
        ro_prog_free(p);
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
        if (i - start > (size_t)ROSHELL_LINE_MAX - 1U) {
            ro_err(err, RO_ERR_LIMIT, (uint16_t)(k + 1U), "line too long");
            goto fail;
        }
        if (i < len) {
            i++;                          /* 跳过换行 */
        }
    }
    for (k = 0U; k < nlines; k++) {
        const char *line = p->text + p->line_off[k];
        int tn = ro_lex(line, toks, ROSHELL_TOKEN_MAX);
        const ro_tok_t *t = toks;
        int kind;
        int last = tn - 2;                /* 末个实 token 下标 */
        if (t[0].type == RO_TOK_EOF) {
            continue;                     /* 空行 / 纯注释行 */
        }
        if (t[0].type == RO_TOK_VAR) {    /* 赋值 / 后缀自增减语句 */
#if ROSHELL_ENABLE_EXPR
            if (ro_op(&t[1], "=")) {
                if (t[2].type == RO_TOK_EOF) {
                    ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "bad assignment");
                    goto fail;
                }
            } else if (ro_op(&t[1], "++") || ro_op(&t[1], "--")) {
                if (t[2].type != RO_TOK_EOF) {
                    ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "bad assignment");
                    goto fail;
                }
            } else {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "bad assignment");
                goto fail;
            }
#else
            ro_err(err, RO_ERR_UNSUPPORTED, (uint16_t)(k + 1U),
                   "assignment requires expressions");
            goto fail;
#endif
            continue;
        }
        if (t[0].type != RO_TOK_WORD) {   /* 表达式语句 */
#if !ROSHELL_ENABLE_EXPR
            ro_err(err, RO_ERR_UNSUPPORTED, (uint16_t)(k + 1U),
                   "expressions disabled");
            goto fail;
#else
            (void)last;
#endif
            continue;
        }
        kind = ro_line_kind(line);
        switch (kind) {
        case K_IF:
#if ROSHELL_ENABLE_EXPR
            if (t[1].type == RO_TOK_EOF) {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "if requires condition");
                goto fail;
            }
            if (depth >= ROSHELL_BLOCK_DEPTH) {
                ro_err(err, RO_ERR_LIMIT, (uint16_t)(k + 1U), "block nesting too deep");
                goto fail;
            }
            pend[depth].kind = K_IF;
            pend[depth].line = k;
            pend[depth].has_else = false;
            depth++;
#else
            ro_err(err, RO_ERR_UNSUPPORTED, (uint16_t)(k + 1U), "if requires expressions");
            goto fail;
#endif
            break;
        case K_FOR:
#if ROSHELL_ENABLE_EXPR
            if (!ro_kw(&t[last], "do")) {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "for requires do");
                goto fail;
            }
            {
                int start = (t[1].type == RO_TOK_VAR && ro_op(&t[2], "=")) ? 3 : 1;
                if (start > last - 1) {
                    ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "for requires list");
                    goto fail;
                }
            }
            if (depth >= ROSHELL_BLOCK_DEPTH) {
                ro_err(err, RO_ERR_LIMIT, (uint16_t)(k + 1U), "block nesting too deep");
                goto fail;
            }
            pend[depth].kind = K_FOR;
            pend[depth].line = k;
            pend[depth].has_else = false;
            depth++;
#else
            ro_err(err, RO_ERR_UNSUPPORTED, (uint16_t)(k + 1U), "for requires expressions");
            goto fail;
#endif
            break;
        case K_WHILE:
#if ROSHELL_ENABLE_EXPR
            if (t[1].type == RO_TOK_EOF) {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "while requires condition");
                goto fail;
            }
            if (depth >= ROSHELL_BLOCK_DEPTH) {
                ro_err(err, RO_ERR_LIMIT, (uint16_t)(k + 1U), "block nesting too deep");
                goto fail;
            }
            pend[depth].kind = K_WHILE;
            pend[depth].line = k;
            pend[depth].has_else = false;
            depth++;
#else
            ro_err(err, RO_ERR_UNSUPPORTED, (uint16_t)(k + 1U), "while requires expressions");
            goto fail;
#endif
            break;
        case K_FUNCTION: {
            uint16_t j;
            uint8_t argc = 0U;
            uint8_t c;
            uint16_t f;
            ro_func_t *fn;
            if (t[1].type != RO_TOK_WORD || t[1].len >= ROSHELL_NAME_MAX) {
                ro_err(err, RO_ERR_LIMIT, (uint16_t)(k + 1U), "bad function name");
                goto fail;
            }
            if (!ro_op(&t[2], "(")) {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "bad function header");
                goto fail;
            }
            j = 3U;
            while (t[j].type == RO_TOK_VAR) {
                if (argc >= ROSHELL_FUNC_ARGS_MAX) {
                    ro_err(err, RO_ERR_LIMIT, (uint16_t)(k + 1U), "too many arguments");
                    goto fail;
                }
                if (t[j].len >= ROSHELL_VAR_NAME_MAX) {
                    ro_err(err, RO_ERR_LIMIT, (uint16_t)(k + 1U), "parameter name too long");
                    goto fail;
                }
                argc++;
                if (!ro_op(&t[j + 1U], ",")) {
                    j++;
                    break;
                }
                j += 2U;
            }
            if (!ro_op(&t[j], ")") || (int)j != last) {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "bad function header");
                goto fail;
            }
#if !ROSHELL_ENABLE_EXPR
            if (argc > 0U) {
                ro_err(err, RO_ERR_UNSUPPORTED, (uint16_t)(k + 1U),
                       "function arguments require expressions");
                goto fail;
            }
#endif
            for (f = 0U; f < p->func_count; f++) {
                if (strlen(p->funcs[f].name) == (size_t)t[1].len &&
                    memcmp(p->funcs[f].name, t[1].p, t[1].len) == 0) {
                    ro_err(err, RO_ERR_DUPLICATE, (uint16_t)(k + 1U), "duplicate function");
                    goto fail;
                }
            }
            if (p->func_count >= ROSHELL_FUNC_MAX) {
                ro_err(err, RO_ERR_LIMIT, (uint16_t)(k + 1U), "too many functions");
                goto fail;
            }
            fn = &p->funcs[p->func_count];
            memcpy(fn->name, t[1].p, t[1].len);
            fn->name[t[1].len] = '\0';
            fn->start = k;
            fn->argc = argc;
            j = 3U;
            for (c = 0U; c < argc; c++) {
                memcpy(fn->args[c], t[j].p + 1, t[j].len - 1U);
                fn->args[c][t[j].len - 1U] = '\0';
                j += ro_op(&t[j + 1U], ",") ? 2U : 1U;
            }
            p->func_count++;
            if (depth >= ROSHELL_BLOCK_DEPTH) {
                ro_err(err, RO_ERR_LIMIT, (uint16_t)(k + 1U), "block nesting too deep");
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
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U),
                       (kind == K_ELSEIF) ? "elseif without if" : "else without if");
                goto fail;
            }
            if (kind == K_ELSEIF) {
                if (pend[depth - 1U].has_else) {
                    ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "elseif after else");
                    goto fail;
                }
#if ROSHELL_ENABLE_EXPR
                if (t[1].type == RO_TOK_EOF) {
                    ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "elseif requires condition");
                    goto fail;
                }
#endif
            } else {
                if (pend[depth - 1U].has_else) {
                    ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "duplicate else");
                    goto fail;
                }
                pend[depth - 1U].has_else = true;
            }
            break;
        case K_END:
            if (depth == 0U) {
                ro_err(err, RO_ERR_BLOCK, (uint16_t)(k + 1U), "unmatched end");
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
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "break outside loop");
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
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "return outside function");
                goto fail;
            }
            break;
        }
        case K_CALL:
        case K_RUN:
            if (t[1].type != RO_TOK_WORD || t[1].len >= ROSHELL_NAME_MAX ||
                !ro_op(&t[2], "(") || !ro_op(&t[last], ")")) {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U),
                       (kind == K_CALL) ? "call requires ()" : "run requires ()");
                goto fail;
            }
#if !ROSHELL_ENABLE_EXPR
            if (tn != 5) {
                ro_err(err, RO_ERR_UNSUPPORTED, (uint16_t)(k + 1U),
                       "arguments require expressions");
                goto fail;
            }
#endif
            break;
        case K_WAIT:
            if (!ro_kw(&t[1], "time")) {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "wait requires time");
                goto fail;
            }
#if ROSHELL_ENABLE_EXPR
            if (t[2].type == RO_TOK_EOF) {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "wait requires value");
                goto fail;
            }
#else
            if (t[2].type != RO_TOK_NUMBER || t[2].is_float) {
                ro_err(err, RO_ERR_UNSUPPORTED, (uint16_t)(k + 1U),
                       "wait requires integer");
                goto fail;
            }
#endif
            break;
        case K_LOAD:
            if (t[1].type != RO_TOK_STRING) {
                ro_err(err, RO_ERR_PARSE, (uint16_t)(k + 1U), "load requires string");
                goto fail;
            }
            break;
        default:                           /* 非关键字 WORD：表达式语句（调用等） */
#if !ROSHELL_ENABLE_EXPR
            ro_err(err, RO_ERR_UNSUPPORTED, (uint16_t)(k + 1U), "expressions disabled");
            goto fail;
#endif
            break;
        }
    }
    if (depth > 0U) {
        ro_err(err, RO_ERR_BLOCK, (uint16_t)(pend[depth - 1U].line + 1U), "unclosed block");
        goto fail;
    }
    ros_mem_free(toks);
    p->refs = 1U;
    return p;
fail:
    ros_mem_free(toks);
    ro_prog_free(p);
    return NULL;
}

/* ============================== 运行时 ============================== */

static int ro_exec_unit(ro_env_t *env);
static int ro_do_load(ro_env_t *env, const char *path);
static int ro_slot_start(roshell_t *rs, ro_prog_t *prog, const char *fname,
                         uint16_t flen, const ro_value_t *args, uint8_t argc);
static void ro_run_entry(void *arg);

#if ROSHELL_ENABLE_EXPR

static int ro_frame_push(ro_ctx_t *ctx, uint8_t type, uint16_t start, uint16_t end)
{
    ro_frame_t *f;
    if (ctx->depth >= ROSHELL_BLOCK_DEPTH) {
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
static void ro_if_advance(ro_env_t *env, uint16_t hdr)
{
    ro_ctx_t *ctx = env->ctx;
    ro_frame_t *f = &ctx->frames[ctx->depth - 1U];
    uint16_t i;
    int d = 0;
    if (f->taken) {
        ctx->pc = (uint16_t)(hdr + 1U);
        return;
    }
    for (i = (uint16_t)(hdr + 1U); i < f->end; i++) {
        int kind = ro_line_kind(env->prog->text + env->prog->line_off[i]);
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

static int ro_stmt_assign(ro_env_t *env, const ro_tok_t *t, int tn)
{
    ro_value_t v;
    ro_var_t *var;
    int rc;
    if (ro_op(&t[1], "=")) {
        rc = ro_eval_tokens(env, t, tn, 2, &v);
        if (rc != RO_OK) {
            if (rc == RO_ERR_PARSE) {
                ro_err0(env->err, rc, "bad assignment");
            }
            return rc;
        }
        if (v.type == RO_VAL_NONE) {
            ro_err0(env->err, RO_ERR_TYPE, "no return value");
            return RO_ERR_TYPE;
        }
        var = ro_var_alloc(env->ctx, t[0].p + 1, (uint16_t)(t[0].len - 1U));
        if (var == NULL) {
            ro_err0(env->err, RO_ERR_LIMIT, "too many variables");
            return RO_ERR_LIMIT;
        }
        ro_var_store(var, &v);
        return RO_OK;
    }
    var = ro_var_find(env->ctx, t[0].p + 1, (uint16_t)(t[0].len - 1U));
    if (var == NULL) {
        ro_err0(env->err, RO_ERR_UNDEF_VAR, "undefined variable");
        return RO_ERR_UNDEF_VAR;
    }
    if (var->type != RO_VAL_INT && var->type != RO_VAL_FLOAT) {
        return ro_type_err(env, "type mismatch");
    }
    if (var->type == RO_VAL_INT) {
        var->i += ro_op(&t[1], "++") ? 1 : -1;
    } else {
        var->f += ro_op(&t[1], "++") ? 1.0f : -1.0f;
    }
    return RO_OK;
}

typedef struct { const char *p; uint16_t len; } ro_seg_t;

/* 解析 for 行：顶层逗号切出列表段（指向 prog->text 原文），可选 $i= 绑定 */
static int ro_for_parse(ro_env_t *env, const ro_tok_t *t, int tn,
                        ro_seg_t *items, uint16_t *count,
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
    if (!ro_kw(&t[last], "do")) {
        ro_err0(env->err, RO_ERR_PARSE, "for requires do");
        return RO_ERR_PARSE;
    }
    if (t[1].type == RO_TOK_VAR && ro_op(&t[2], "=")) {
        *bp = t[1].p + 1;
        *blen = (uint16_t)(t[1].len - 1U);
        start = 3;
    } else {
        start = 1;
    }
    for (i = start; i < last; i++) {
        if (t[i].type == RO_TOK_OP) {
            if (ro_op(&t[i], "(")) {
                d++;
            } else if (ro_op(&t[i], ")")) {
                d--;
            } else if (ro_op(&t[i], ",") && d == 0) {
                if (seg < 0) {
                    ro_err0(env->err, RO_ERR_PARSE, "for requires list");
                    return RO_ERR_PARSE;
                }
                if (c >= ROSHELL_FOR_ITEMS_MAX) {
                    ro_err0(env->err, RO_ERR_LIMIT, "too many items");
                    return RO_ERR_LIMIT;
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
        ro_err0(env->err, RO_ERR_PARSE, "for requires list");
        return RO_ERR_PARSE;
    }
    if (c >= ROSHELL_FOR_ITEMS_MAX) {
        ro_err0(env->err, RO_ERR_LIMIT, "too many items");
        return RO_ERR_LIMIT;
    }
    items[c].p = t[seg].p;
    items[c].len = (uint16_t)((t[last - 1].p + t[last - 1].len) - t[seg].p);
    *count = (uint16_t)(c + 1U);
    return RO_OK;
}

/* 惰性求值第 idx 项：原文拷入栈缓冲后重新词法（覆盖 ctx->toks 安全） */
static int ro_for_step(ro_env_t *env, const ro_tok_t *t, int tn, uint16_t idx)
{
    ro_seg_t items[ROSHELL_FOR_ITEMS_MAX];
    uint16_t count = 0U;
    const char *bp;
    uint16_t blen;
    ro_value_t v;
    char ibuf[ROSHELL_LINE_MAX];
    int rc;
    rc = ro_for_parse(env, t, tn, items, &count, &bp, &blen);
    if (rc != RO_OK || idx >= count) {
        return rc;
    }
    if (items[idx].len >= ROSHELL_LINE_MAX) {
        ro_err0(env->err, RO_ERR_LIMIT, "item too long");
        return RO_ERR_LIMIT;
    }
    memcpy(ibuf, items[idx].p, items[idx].len);
    ibuf[items[idx].len] = '\0';
    tn = ro_lex(ibuf, env->ctx->toks, ROSHELL_TOKEN_MAX);
    rc = ro_eval_tokens(env, env->ctx->toks, tn, 0, &v);
    if (rc != RO_OK) {
        return rc;
    }
    if (bp != NULL) {
        ro_var_t *var = ro_var_alloc(env->ctx, bp, blen);
        if (var == NULL) {
            ro_err0(env->err, RO_ERR_LIMIT, "too many variables");
            return RO_ERR_LIMIT;
        }
        ro_var_store(var, &v);
    }
    return RO_OK;
}

#endif /* ROSHELL_ENABLE_EXPR */

/* call/run 语句的实参列表：t[0]=关键字 t[1]=名 t[2]='(' */
static int ro_stmt_args(ro_env_t *env, const ro_tok_t *t, int tn,
                        ro_value_t *args, uint8_t *argc)
{
#if ROSHELL_ENABLE_EXPR
    ro_ps_t ps;
    ps.t = t;
    ps.n = tn;
    ps.pos = 2;
    ps.noeval = false;
    return ro_p_args(env, &ps, args, argc);
#else
    (void)env;
    (void)t;
    (void)tn;
    (void)args;
    *argc = 0U;
    return RO_OK;
#endif
}

static int ro_stmt_call(ro_env_t *env, const ro_tok_t *t, int tn)
{
    ro_value_t args[ROSHELL_FUNC_ARGS_MAX];
    uint8_t argc = 0U;
    const ro_func_t *f;
    int rc = ro_stmt_args(env, t, tn, args, &argc);
    if (rc != RO_OK) {
        return rc;
    }
    f = ro_find_func(env->prog, t[1].p, t[1].len);
    if (f == NULL) {
        ro_err0(env->err, RO_ERR_UNDEF_FUNC, "undefined function");
        return RO_ERR_UNDEF_FUNC;
    }
    if (argc != f->argc) {
        ro_err0(env->err, RO_ERR_ARG_COUNT, "arg count mismatch");
        return RO_ERR_ARG_COUNT;
    }
    if ((uint32_t)env->depth + 1U > ROSHELL_CALL_DEPTH) {
        ro_err0(env->err, RO_ERR_STACK_OVERFLOW, "call stack overflow");
        return RO_ERR_STACK_OVERFLOW;
    }
    return ro_call_body(env->rs, env->prog, f, args,
                        (uint8_t)(env->depth + 1U), env->ctx, NULL, env->err);
}

static int ro_stmt_run(ro_env_t *env, const ro_tok_t *t, int tn)
{
    ro_value_t args[ROSHELL_FUNC_ARGS_MAX];
    uint8_t argc = 0U;
    const ro_func_t *f;
    int rc = ro_stmt_args(env, t, tn, args, &argc);
    if (rc != RO_OK) {
        return rc;
    }
    f = ro_find_func(env->prog, t[1].p, t[1].len);
    if (f == NULL) {
        ro_err0(env->err, RO_ERR_UNDEF_FUNC, "undefined function");
        return RO_ERR_UNDEF_FUNC;
    }
    if (argc != f->argc) {
        ro_err0(env->err, RO_ERR_ARG_COUNT, "arg count mismatch");
        return RO_ERR_ARG_COUNT;
    }
    rc = ro_slot_start(env->rs, env->prog, t[1].p, t[1].len, args, argc);
    if (rc < 0) {
        ro_err0(env->err, rc,
               (rc == RO_ERR_LIMIT) ? "run table full" : "task create failed");
        return rc;
    }
    return RO_OK;
}

static int ro_do_load(ro_env_t *env, const char *path)
{
#if ROSHELL_HAS_FS
    ro_prog_t *sub;
    ro_ctx_t child;
    ro_env_t cenv;
    char *buf;
    size_t got;
    FILE *fp;
    int rc;
    fp = fopen(path, "rb");
    if (fp == NULL) {
        ro_err0(env->err, RO_ERR_NOT_FOUND, "file not found");
        return RO_ERR_NOT_FOUND;
    }
    buf = (char *)ros_mem_malloc(ROSHELL_SCRIPT_MAX + 1U);
    if (buf == NULL) {
        fclose(fp);
        ro_err0(env->err, RO_ERR_RESOURCE, "out of memory");
        return RO_ERR_RESOURCE;
    }
    got = fread(buf, 1U, ROSHELL_SCRIPT_MAX, fp);
    fclose(fp);
    buf[got] = '\0';
    sub = ro_scan(buf, env->err);
    ros_mem_free(buf);
    if (sub == NULL) {
        return (env->err != NULL) ? env->err->code : RO_ERR_PARSE;
    }
    memset(&child, 0, sizeof(child));
    child.vars = env->ctx->vars;            /* 共享父变量表（规格 §4.7） */
    child.toks = (ro_tok_t *)ros_mem_malloc(ROSHELL_TOKEN_MAX * sizeof(ro_tok_t));
    if (child.toks == NULL) {
        ro_prog_release(sub);
        ro_err0(env->err, RO_ERR_RESOURCE, "out of memory");
        return RO_ERR_RESOURCE;
    }
    cenv.rs = env->rs;
    cenv.prog = sub;
    cenv.ctx = &child;
    cenv.depth = env->depth;
    cenv.err = env->err;
    rc = ro_exec_unit(&cenv);
    ros_mem_free(child.toks);
    ro_prog_release(sub);
    return rc;
#else
    (void)path;
    ro_err0(env->err, RO_ERR_UNSUPPORTED, "load unsupported");
    return RO_ERR_UNSUPPORTED;
#endif
}

/* 函数体执行：私有变量表 + token 缓冲；返回字符串拷回 caller 临时槽 */
static int ro_call_body(roshell_t *rs, ro_prog_t *prog, const ro_func_t *f,
                        const ro_value_t *args, uint8_t depth,
                        ro_ctx_t *caller, ro_value_t *out, roshell_err_t *err)
{
    ro_ctx_t child;
    ro_env_t cenv;
    int rc;
#if ROSHELL_ENABLE_EXPR
    uint8_t i;
#endif
    memset(&child, 0, sizeof(child));
    child.vars = (ro_var_t *)ros_mem_calloc(ROSHELL_VAR_MAX, sizeof(ro_var_t));
    child.toks = (ro_tok_t *)ros_mem_malloc(ROSHELL_TOKEN_MAX * sizeof(ro_tok_t));
    if (child.vars == NULL || child.toks == NULL) {
        ros_mem_free(child.vars);
        ros_mem_free(child.toks);
        ro_err0(err, RO_ERR_RESOURCE, "out of memory");
        return RO_ERR_RESOURCE;
    }
#if ROSHELL_ENABLE_EXPR
    child.own_vars = true;
    for (i = 0U; i < f->argc; i++) {
        ro_var_t *v = ro_var_alloc(&child, f->args[i],
                                   (uint16_t)strlen(f->args[i]));
        if (v == NULL) {
            ros_mem_free(child.vars);
            ros_mem_free(child.toks);
            ro_err0(err, RO_ERR_LIMIT, "too many variables");
            return RO_ERR_LIMIT;
        }
        ro_var_store(v, &args[i]);
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
    rc = ro_exec_unit(&cenv);
#if ROSHELL_ENABLE_EXPR
    if (out != NULL) {
        *out = child.ret;
        if (rc != RO_OK) {
            out->type = RO_VAL_NONE;
        } else if (out->type == RO_VAL_STRING && caller != NULL) {
            out->s = ro_tmp(caller, out->s, out->slen, &out->slen);
        }
    }
#endif
    ros_mem_free(child.vars);
    ros_mem_free(child.toks);
    return rc;
}

/* 按行号 PC 解释执行一个执行单元（脚本 / 函数体 / load 子单元） */
static int ro_exec_unit(ro_env_t *env)
{
    ro_ctx_t *ctx = env->ctx;
    ro_prog_t *prog = env->prog;
    roshell_err_t *err = env->err;
    int rc;
    while (!ctx->returned && ctx->pc < prog->line_count) {
        uint16_t ln = ctx->pc;
        const char *line = prog->text + prog->line_off[ln];
        ro_tok_t *t = ctx->toks;
        int tn = ro_lex(line, t, ROSHELL_TOKEN_MAX);
        int kind;
        ctx->pc = (uint16_t)(ln + 1U);
        if (err != NULL) {
            err->line = (uint16_t)(ln + 1U);
        }
        if (t[0].type == RO_TOK_EOF) {
            continue;
        }
        if (t[0].type == RO_TOK_VAR) {
#if ROSHELL_ENABLE_EXPR
            rc = ro_stmt_assign(env, t, tn);
#else
            ro_err0(err, RO_ERR_UNSUPPORTED, "assignment requires expressions");
            rc = RO_ERR_UNSUPPORTED;
#endif
            if (rc != RO_OK) {
                return rc;
            }
            continue;
        }
        kind = ro_line_kind(line);
        switch (kind) {
        case K_NONE:
#if ROSHELL_ENABLE_EXPR
            {
                ro_value_t v;
                rc = ro_eval_tokens(env, t, tn, 0, &v);
                if (rc == RO_ERR_PARSE) {
                    ro_err0(err, rc, "unknown statement");
                }
            }
#else
            ro_err0(err, RO_ERR_UNSUPPORTED, "expressions disabled");
            rc = RO_ERR_UNSUPPORTED;
#endif
            if (rc != RO_OK) {
                return rc;
            }
            break;
        case K_IF:
#if ROSHELL_ENABLE_EXPR
            {
                bool cond = false;
                rc = ro_eval_cond(env, t, tn, 1, &cond);
                if (rc != RO_OK) {
                    return rc;
                }
                if (ro_frame_push(ctx, RO_BLK_IF, ln, prog->end_of[ln]) != 0) {
                    ro_err0(err, RO_ERR_LIMIT, "block nesting too deep");
                    return RO_ERR_LIMIT;
                }
                ctx->frames[ctx->depth - 1U].taken = cond;
                ro_if_advance(env, ln);
            }
#else
            ro_err0(err, RO_ERR_UNSUPPORTED, "if requires expressions");
            return RO_ERR_UNSUPPORTED;
#endif
            break;
        case K_ELSEIF:
        case K_ELSE: {
            ro_frame_t *f;
            if (ctx->depth == 0U || ctx->frames[ctx->depth - 1U].type != RO_BLK_IF) {
                ro_err0(err, RO_ERR_BLOCK, "else without if");
                return RO_ERR_BLOCK;
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
#if ROSHELL_ENABLE_EXPR
            rc = ro_eval_cond(env, t, tn, 1, &f->taken);
            if (rc != RO_OK) {
                return rc;
            }
            ro_if_advance(env, ln);
#else
            ro_err0(err, RO_ERR_UNSUPPORTED, "elseif requires expressions");
            return RO_ERR_UNSUPPORTED;
#endif
            break;
        }
        case K_END: {
            ro_frame_t *f;
            if (ctx->depth == 0U) {
                if (ctx->in_func) {
                    ctx->returned = true;
                    return RO_OK;
                }
                ro_err0(err, RO_ERR_BLOCK, "unmatched end");
                return RO_ERR_BLOCK;
            }
            f = &ctx->frames[ctx->depth - 1U];
            if (f->type == RO_BLK_IF) {
                ctx->depth--;
                ctx->pc = (uint16_t)(ln + 1U);
            } else if (f->type == RO_BLK_WHILE) {
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
#if ROSHELL_ENABLE_EXPR
            {
                ro_frame_t *f;
                if (ctx->depth > 0U) {
                    f = &ctx->frames[ctx->depth - 1U];
                    if (f->type == RO_BLK_FOR && f->start == ln) {
                        rc = ro_for_step(env, t, tn, f->idx);
                        if (rc != RO_OK) {
                            return rc;
                        }
                        ctx->pc = (uint16_t)(ln + 1U);
                        break;
                    }
                }
                {
                    ro_seg_t items[ROSHELL_FOR_ITEMS_MAX];
                    uint16_t count = 0U;
                    const char *bp;
                    uint16_t blen;
                    rc = ro_for_parse(env, t, tn, items, &count, &bp, &blen);
                    if (rc != RO_OK) {
                        return rc;
                    }
                    if (ro_frame_push(ctx, RO_BLK_FOR, ln, prog->end_of[ln]) != 0) {
                        ro_err0(err, RO_ERR_LIMIT, "block nesting too deep");
                        return RO_ERR_LIMIT;
                    }
                    ctx->frames[ctx->depth - 1U].count = count;
                }
                rc = ro_for_step(env, t, tn, 0U);
                if (rc != RO_OK) {
                    return rc;
                }
                ctx->pc = (uint16_t)(ln + 1U);
            }
#else
            ro_err0(err, RO_ERR_UNSUPPORTED, "for requires expressions");
            return RO_ERR_UNSUPPORTED;
#endif
            break;
        case K_WHILE:
#if ROSHELL_ENABLE_EXPR
            {
                bool cond = false;
                bool reentry = (ctx->depth > 0U &&
                                ctx->frames[ctx->depth - 1U].type == RO_BLK_WHILE &&
                                ctx->frames[ctx->depth - 1U].start == ln);
                rc = ro_eval_cond(env, t, tn, 1, &cond);
                if (rc != RO_OK) {
                    return rc;
                }
                if (cond) {
                    if (!reentry &&
                        ro_frame_push(ctx, RO_BLK_WHILE, ln, prog->end_of[ln]) != 0) {
                        ro_err0(err, RO_ERR_LIMIT, "block nesting too deep");
                        return RO_ERR_LIMIT;
                    }
                } else {
                    if (reentry) {
                        ctx->depth--;
                    }
                    ctx->pc = (uint16_t)(prog->end_of[ln] + 1U);
                }
            }
#else
            ro_err0(err, RO_ERR_UNSUPPORTED, "while requires expressions");
            return RO_ERR_UNSUPPORTED;
#endif
            break;
        case K_BREAK: {
            int k = -1;
            uint8_t j;
            uint16_t be;
            for (j = ctx->depth; j > 0U; j--) {
                if (ctx->frames[j - 1U].type != RO_BLK_IF) {
                    k = (int)(j - 1U);
                    break;
                }
            }
            if (k < 0) {
                ro_err0(err, RO_ERR_PARSE, "break outside loop");
                return RO_ERR_PARSE;
            }
            be = ctx->frames[k].end;
            ctx->depth = (uint8_t)k;
            ctx->pc = (uint16_t)(be + 1U);
            break;
        }
        case K_FUNCTION:
            ctx->pc = (uint16_t)(prog->end_of[ln] + 1U);
            break;
        case K_CALL:
            rc = ro_stmt_call(env, t, tn);
            if (rc != RO_OK) {
                return rc;
            }
            break;
        case K_RUN:
            rc = ro_stmt_run(env, t, tn);
            if (rc != RO_OK) {
                return rc;
            }
            break;
        case K_RETURN:
            if (!ctx->in_func) {
                ro_err0(err, RO_ERR_PARSE, "return outside function");
                return RO_ERR_PARSE;
            }
            if (t[1].type != RO_TOK_EOF) {
#if ROSHELL_ENABLE_EXPR
                rc = ro_eval_tokens(env, t, tn, 1, &ctx->ret);
                if (rc != RO_OK) {
                    return rc;
                }
#else
                ro_err0(err, RO_ERR_UNSUPPORTED, "return requires expressions");
                return RO_ERR_UNSUPPORTED;
#endif
            }
            ctx->returned = true;
            break;
        case K_WAIT:
#if ROSHELL_ENABLE_EXPR
            {
                ro_value_t v;
                uint32_t ms;
                rc = ro_eval_tokens(env, t, tn, 2, &v);
                if (rc != RO_OK) {
                    return rc;
                }
                if (v.type == RO_VAL_INT) {
                    ms = (v.i < 0) ? 0U : (uint32_t)v.i;
                } else if (v.type == RO_VAL_FLOAT) {
                    ms = (v.f < 0.0f) ? 0U : (uint32_t)v.f;
                } else {
                    ro_err0(err, RO_ERR_TYPE, "wait needs number");
                    return RO_ERR_TYPE;
                }
                ros_sleep_ms(ms);
            }
#else
            ros_sleep_ms((uint32_t)t[2].ival);
#endif
            break;
        case K_LOAD: {
            char path[ROSHELL_LINE_MAX];
            uint16_t plen = 0U;
            if (!ro_str_unescape(&t[1], path, sizeof(path), &plen)) {
                ro_err0(err, RO_ERR_PARSE, "unclosed string");
                return RO_ERR_PARSE;
            }
            rc = ro_do_load(env, path);
            if (rc != RO_OK) {
                return rc;
            }
            break;
        }
        default:
            ro_err0(err, RO_ERR_PARSE, "unknown statement");
            return RO_ERR_PARSE;
        }
    }
    return RO_OK;
}

/* ============================== run 槽位 / 任务 ============================== */

static void ro_slot_reap(ro_slot_t *s)
{
    if (s->state == RO_RUN_RUNNING || s->task == NULL) {
        return;
    }
    if (s->task->state == ROS_TASK_TERMINATED) {
        ros_task_destroy(s->task);
        s->task = NULL;
        s->state = RO_RUN_FREE;
        s->fname[0] = '\0';
    }
}

static void ro_run_entry(void *arg)
{
    ro_slot_t *s = (ro_slot_t *)arg;
    roshell_err_t e;
    ro_ctx_t ctx;
    ro_env_t env;
    int rc;
    memset(&e, 0, sizeof(e));
    memset(&ctx, 0, sizeof(ctx));
    env.rs = s->owner;
    env.prog = s->prog;
    env.ctx = &ctx;
    env.depth = 0U;
    env.err = &e;
    if (s->fname[0] != '\0') {
        const ro_func_t *f = ro_find_func(s->prog, s->fname,
                                          (uint16_t)strlen(s->fname));
        if (f == NULL) {
            rc = RO_ERR_UNDEF_FUNC;
        } else if (f->argc != s->argc) {
            rc = RO_ERR_ARG_COUNT;
        } else {
            rc = ro_call_body(s->owner, s->prog, f, s->args, 0U, NULL, NULL, &e);
        }
    } else {
        ctx.vars = (ro_var_t *)ros_mem_calloc(ROSHELL_VAR_MAX, sizeof(ro_var_t));
        ctx.toks = (ro_tok_t *)ros_mem_malloc(ROSHELL_TOKEN_MAX * sizeof(ro_tok_t));
        if (ctx.vars == NULL || ctx.toks == NULL) {
            rc = RO_ERR_RESOURCE;
        } else {
            rc = ro_exec_unit(&env);
        }
        ros_mem_free(ctx.vars);
        ros_mem_free(ctx.toks);
    }
    s->code = rc;
    s->state = (rc == RO_OK) ? RO_RUN_DONE : RO_RUN_ERROR;
    ro_prog_release(s->prog);
    s->prog = NULL;
    ros_task_exit();
}

static int ro_slot_start(roshell_t *rs, ro_prog_t *prog, const char *fname,
                         uint16_t flen, const ro_value_t *args, uint8_t argc)
{
    ro_slot_t *s = NULL;
    ros_task_t *task;
    uint32_t i;
    uint32_t free_idx = ROSHELL_RUN_MAX;
    for (i = 0U; i < ROSHELL_RUN_MAX; i++) {
        ro_slot_reap(&rs->slots[i]);
        if (s == NULL && rs->slots[i].state == RO_RUN_FREE) {
            s = &rs->slots[i];
            free_idx = i;
        }
    }
    if (s == NULL) {
        return RO_ERR_LIMIT;
    }
    memset(s, 0, sizeof(*s));
    s->owner = rs;
    if (flen > 0U && flen < ROSHELL_NAME_MAX) {
        memcpy(s->fname, fname, flen);
        s->fname[flen] = '\0';
    }
    for (i = 0U; i < argc; i++) {
        s->args[i] = args[i];
        if (args[i].type == RO_VAL_STRING) {
            memcpy(s->argstr[i], args[i].s, args[i].slen);
            s->argstr[i][args[i].slen] = '\0';
            s->args[i].s = s->argstr[i];
        }
    }
    s->argc = argc;
    task = ros_task_create_dynamic("roshell", ROSHELL_RUN_PRIO, ro_run_entry,
                                   s, ROSHELL_RUN_STACK);
    if (task == NULL) {
        return RO_ERR_RESOURCE;
    }
    s->task = task;
    s->prog = prog;
    prog->refs++;
    s->state = RO_RUN_RUNNING;
    return (int)free_idx;
}

/* ============================== 公开 API ============================== */

roshell_t *roshell_create(void)
{
    roshell_t *rs = (roshell_t *)ros_mem_calloc(1U, sizeof(*rs));
    if (rs == NULL) {
        return NULL;
    }
    rs->top.vars = (ro_var_t *)ros_mem_calloc(ROSHELL_VAR_MAX, sizeof(ro_var_t));
    rs->top.toks = (ro_tok_t *)ros_mem_malloc(ROSHELL_TOKEN_MAX * sizeof(ro_tok_t));
    if (rs->top.vars == NULL || rs->top.toks == NULL) {
        roshell_destroy(rs);
        return NULL;
    }
    rs->top.own_vars = true;
    return rs;
}

void roshell_destroy(roshell_t *rs)
{
    uint32_t i;
    if (rs == NULL) {
        return;
    }
    for (i = 0U; i < ROSHELL_RUN_MAX; i++) {
        ro_slot_t *s = &rs->slots[i];
        if (s->state == RO_RUN_RUNNING) {
            continue;                       /* 运行中任务：跳过，由任务自行释放 */
        }
        if (s->task != NULL) {
            if (s->task->state == ROS_TASK_TERMINATED &&
                ros_task_destroy(s->task) == ROS_OK) {
                s->task = NULL;
            }
        }
        if (s->prog != NULL) {
            ro_prog_release(s->prog);
            s->prog = NULL;
        }
    }
    ros_mem_free(rs->top.vars);
    ros_mem_free(rs->top.toks);
    ros_mem_free(rs);
}

int roshell_exec(roshell_t *rs, const char *script, roshell_err_t *err)
{
    ro_prog_t *prog;
    ro_env_t env;
    int rc;
    if (rs == NULL || script == NULL) {
        ro_err(err, RO_ERR_PARAM, 0U, "bad parameter");
        return RO_ERR_PARAM;
    }
    prog = ro_scan(script, err);
    if (prog == NULL) {
        return (err != NULL) ? err->code : RO_ERR_PARSE;
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
    rc = ro_exec_unit(&env);
    ro_prog_release(prog);
    return rc;
}

int roshell_exec_line(roshell_t *rs, const char *line, roshell_err_t *err)
{
    if (rs == NULL || line == NULL) {
        ro_err(err, RO_ERR_PARAM, 0U, "bad parameter");
        return RO_ERR_PARAM;
    }
    switch (ro_line_kind(line)) {
    case K_IF: case K_ELSEIF: case K_ELSE: case K_END:
    case K_FOR: case K_WHILE: case K_FUNCTION:
        ro_err(err, RO_ERR_PARSE, 1U, "block statement requires script");
        return RO_ERR_PARSE;
    default:
        break;
    }
    return roshell_exec(rs, line, err);
}

int roshell_run_async(roshell_t *rs, const char *script, roshell_err_t *err)
{
    ro_prog_t *prog;
    int slot;
    if (rs == NULL || script == NULL) {
        ro_err(err, RO_ERR_PARAM, 0U, "bad parameter");
        return RO_ERR_PARAM;
    }
    prog = ro_scan(script, err);
    if (prog == NULL) {
        return (err != NULL) ? err->code : RO_ERR_PARSE;
    }
    slot = ro_slot_start(rs, prog, NULL, 0U, NULL, 0U);
    if (slot < 0) {
        ro_err(err, slot, 0U,
               (slot == RO_ERR_LIMIT) ? "run table full" : "task create failed");
        ro_prog_release(prog);
    }
    return slot;
}

int roshell_run_wait(roshell_t *rs, int slot, uint32_t timeout_ms)
{
    ro_slot_t *s;
    uint32_t waited = 0U;
    uint8_t st;
    int code;
    if (rs == NULL || slot < 0 || (uint32_t)slot >= ROSHELL_RUN_MAX) {
        return RO_ERR_PARAM;
    }
    s = &rs->slots[slot];
    while (s->state == RO_RUN_RUNNING) {
        if (timeout_ms != ROS_TIMEOUT_FOREVER && waited >= timeout_ms) {
            return RO_ERR_TIMEOUT;
        }
        ros_sleep_ms(1U);
        waited++;
    }
    st = s->state;
    code = s->code;
    ro_slot_reap(s);
    if (st == RO_RUN_DONE) {
        return RO_OK;
    }
    if (st == RO_RUN_ERROR) {
        return code;
    }
    return RO_ERR_PARAM;
}

int roshell_run_state(const roshell_t *rs, int slot)
{
    if (rs == NULL || slot < 0 || (uint32_t)slot >= ROSHELL_RUN_MAX) {
        return RO_ERR_PARAM;
    }
    return (int)rs->slots[slot].state;
}

#if ROSHELL_HAS_FS
int roshell_load(roshell_t *rs, const char *path, roshell_err_t *err)
{
    ro_env_t env;
    if (rs == NULL || path == NULL) {
        ro_err(err, RO_ERR_PARAM, 0U, "bad parameter");
        return RO_ERR_PARAM;
    }
    env.rs = rs;
    env.prog = NULL;
    env.ctx = &rs->top;
    env.depth = 0U;
    env.err = err;
    if (err != NULL) {
        err->line = 0U;
    }
    return ro_do_load(&env, path);
}
#endif

/* ============================== 顶层变量读取 ============================== */

#if ROSHELL_ENABLE_EXPR
static ro_var_t *ro_top_var(roshell_t *rs, const char *name)
{
    if (name[0] == '$') {
        name++;
    }
    return ro_var_find(&rs->top, name, (uint16_t)strlen(name));
}

int roshell_get_int(roshell_t *rs, const char *name, int32_t *out)
{
    ro_var_t *v;
    if (rs == NULL || name == NULL || out == NULL) {
        return RO_ERR_PARAM;
    }
    v = ro_top_var(rs, name);
    if (v == NULL) {
        return RO_ERR_UNDEF_VAR;
    }
    if (v->type != RO_VAL_INT) {
        return RO_ERR_TYPE;
    }
    *out = v->i;
    return RO_OK;
}

int roshell_get_float(roshell_t *rs, const char *name, float *out)
{
    ro_var_t *v;
    if (rs == NULL || name == NULL || out == NULL) {
        return RO_ERR_PARAM;
    }
    v = ro_top_var(rs, name);
    if (v == NULL) {
        return RO_ERR_UNDEF_VAR;
    }
    if (v->type != RO_VAL_FLOAT) {
        return RO_ERR_TYPE;
    }
    *out = v->f;
    return RO_OK;
}

int roshell_get_bool(roshell_t *rs, const char *name, bool *out)
{
    ro_var_t *v;
    if (rs == NULL || name == NULL || out == NULL) {
        return RO_ERR_PARAM;
    }
    v = ro_top_var(rs, name);
    if (v == NULL) {
        return RO_ERR_UNDEF_VAR;
    }
    if (v->type != RO_VAL_BOOL) {
        return RO_ERR_TYPE;
    }
    *out = v->b;
    return RO_OK;
}

int roshell_get_str(roshell_t *rs, const char *name, char *buf, uint32_t cap)
{
    ro_var_t *v;
    uint32_t n;
    if (rs == NULL || name == NULL || buf == NULL || cap == 0U) {
        return RO_ERR_PARAM;
    }
    v = ro_top_var(rs, name);
    if (v == NULL) {
        return RO_ERR_UNDEF_VAR;
    }
    if (v->type != RO_VAL_STRING) {
        return RO_ERR_TYPE;
    }
    n = v->slen;
    if (n > cap - 1U) {
        n = cap - 1U;
    }
    memcpy(buf, v->str, n);
    buf[n] = '\0';
    return RO_OK;
}

#else /* !ROSHELL_ENABLE_EXPR */

int roshell_get_int(roshell_t *rs, const char *name, int32_t *out)
{
    (void)rs; (void)name; (void)out;
    return RO_ERR_UNSUPPORTED;
}

int roshell_get_float(roshell_t *rs, const char *name, float *out)
{
    (void)rs; (void)name; (void)out;
    return RO_ERR_UNSUPPORTED;
}

int roshell_get_bool(roshell_t *rs, const char *name, bool *out)
{
    (void)rs; (void)name; (void)out;
    return RO_ERR_UNSUPPORTED;
}

int roshell_get_str(roshell_t *rs, const char *name, char *buf, uint32_t cap)
{
    (void)rs; (void)name; (void)buf; (void)cap;
    return RO_ERR_UNSUPPORTED;
}

#endif /* ROSHELL_ENABLE_EXPR */
