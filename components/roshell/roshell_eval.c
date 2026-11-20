/*
 * roshell_eval.c - roshell 表达式求值器（规格 §5）
 *
 * 变量表管理、值格式化与临时字符串槽、递归下降表达式解析求值
 * （字面量 / 变量 / 常量 / 内置与扩展函数调用 / 脚本函数调用 /
 * 算术、比较、逻辑）。整文件由 RS_ENABLE_EXPR 门控。
 */

#include "roshell.h"
#include "roshell_core.h"
#include "roshell_func.h"

#include <string.h>

#if RS_ENABLE_EXPR
/* IEEE754 正无穷（不依赖 math.h，避免常量除零告警） */
static float rs_inf(bool neg)
{
    uint32_t bits = 0x7F800000U;
    float v;
    memcpy(&v, &bits, sizeof(v));
    return neg ? -v : v;
}

#endif /* RS_ENABLE_EXPR */

/* ============================== 值辅助（EXPR） ============================== */

#if RS_ENABLE_EXPR

rs_var_t *rs_var_find(rs_ctx_t *ctx, const char *name, uint16_t len)
{
    uint32_t i;
    if (len == 0U || len >= RS_VAR_NAME_MAX) {
        return NULL;
    }
    for (i = 0U; i < RS_VAR_MAX; i++) {
        rs_var_t *v = &ctx->vars[i];
        if (v->used && strlen(v->name) == (size_t)len &&
            memcmp(v->name, name, len) == 0) {
            return v;
        }
    }
    return NULL;
}

rs_var_t *rs_var_alloc(rs_ctx_t *ctx, const char *name, uint16_t len)
{
    rs_var_t *v;
    uint32_t i;
    if (len == 0U || len >= RS_VAR_NAME_MAX) {
        return NULL;
    }
    v = rs_var_find(ctx, name, len);
    if (v != NULL) {
        return v;
    }
    for (i = 0U; i < RS_VAR_MAX; i++) {
        if (!ctx->vars[i].used) {
            v = &ctx->vars[i];
            break;
        }
    }
    if (i >= RS_VAR_MAX) {
        return NULL;                        /* 变量表满 */
    }
    memset(v, 0, sizeof(*v));
    memcpy(v->name, name, len);
    v->name[len] = '\0';
    v->used = true;
    return v;
}

void rs_var_store(rs_var_t *v, const rs_value_t *val)
{
    v->type = val->type;
    switch (val->type) {
    case RS_VAL_INT:   v->i = val->i; break;
    case RS_VAL_FLOAT: v->f = val->f; break;
    case RS_VAL_BOOL:  v->b = val->b; break;
    default:
        memcpy(v->str, val->s, val->slen);
        v->str[val->slen] = '\0';
        v->slen = val->slen;
        break;
    }
}

/* 临时字符串槽（ping-pong，供字面量 / 拼接 / 返回值拷贝） */
const char *rs_tmp(rs_ctx_t *ctx, const char *src, uint16_t len,
                          uint16_t *out_len)
{
    char *buf = ctx->sbuf[ctx->sflip];
    ctx->sflip = (uint8_t)((ctx->sflip + 1U) % RS_TMP_SLOTS);
    if (len >= RS_STR_MAX) {
        len = RS_STR_MAX - 1U;
    }
    memcpy(buf, src, len);
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

static bool rs_val_is_num(const rs_value_t *v)
{
    return v->type == RS_VAL_INT || v->type == RS_VAL_FLOAT;
}

static float rs_val_fnum(const rs_value_t *v)
{
    return v->type == RS_VAL_FLOAT ? v->f : (float)v->i;
}

static uint16_t rs_fmt_int(int32_t v, char *buf, uint16_t cap)
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
static uint16_t rs_fmt_value(const rs_value_t *v, char *buf, uint16_t cap)
{
    uint16_t o = 0U;
    buf[0] = '\0';
    switch (v->type) {
    case RS_VAL_BOOL: {
        const char *s = v->b ? "true" : "false";
        uint16_t n = (uint16_t)strlen(s);
        if (n >= cap) {
            n = (uint16_t)(cap - 1U);
        }
        memcpy(buf, s, n);
        buf[n] = '\0';
        return n;
    }
    case RS_VAL_INT:
        return rs_fmt_int(v->i, buf, cap);
    case RS_VAL_FLOAT: {
        float f = v->f;
        int32_t ip;
        if (f != f) {
            strncpy(buf, "nan", cap);
            buf[cap - 1U] = '\0';
            return (uint16_t)strlen(buf);
        }
        if (f == rs_inf(false)) {
            strncpy(buf, "inf", cap);
            buf[cap - 1U] = '\0';
            return 3U;
        }
        if (f == rs_inf(true)) {
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
            o += rs_fmt_int(ip, buf + o, (uint16_t)(cap - o));
            return o;
        }
        o += rs_fmt_int(ip, buf + o, (uint16_t)(cap - o));
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

#endif /* RS_ENABLE_EXPR */

/* ============================== 表达式解析（EXPR） ============================== */

#if RS_ENABLE_EXPR


static int rs_p_expr(rs_env_t *env, rs_ps_t *ps, rs_value_t *out);

int rs_type_err(rs_env_t *env, const char *msg)
{
    rs_err0(env->err, RS_ERR_TYPE, msg);
    return RS_ERR_TYPE;
}

static int rs_parse_err(rs_env_t *env, const char *msg)
{
    rs_err0(env->err, RS_ERR_PARSE, msg);
    return RS_ERR_PARSE;
}

/* ============================== 内置函数调用（MATH） ============================== */
/*
 * 内置函数表与常量表定义在 rs_func.c（rs_func_find / rs_const_find），
 * 此处仅保留求值器侧的调用封装；扩展注册表数据在 rs_func.c。
 */

#if RS_ENABLE_MATH

/* 调用内置函数：校验实参个数；numeric 表项（math 函数）要求实参均为数值，
 * 系统函数（tick/uptime/time/date/empty）自行处理实参类型。结果写回 out */
int rs_func_apply(rs_env_t *env, const rs_func_t *m,
                         const rs_value_t *args, uint8_t argc, rs_value_t *out)
{
    uint8_t i;

    if (argc != m->argc) {
        rs_err0(env->err, RS_ERR_ARG_COUNT, "arg count mismatch");
        return RS_ERR_ARG_COUNT;
    }
    if (m->numeric) {
        for (i = 0U; i < argc; i++) {
            if (!rs_val_is_num(&args[i])) {
                return rs_type_err(env, "math needs numbers");
            }
        }
    }
    memset(out, 0, sizeof(*out));
    m->fn(args, out);
    return RS_OK;
}

#endif /* RS_ENABLE_MATH */

/* 调用扩展函数：rs_value_t 实参转 rs_arg_t 值传递（BOOL 以 0/1 整数
 * 传入，字符串实参拒绝），返回值仅接受 INT / FLOAT / BOOL */
int rs_ext_func_apply(rs_env_t *env, const rs_ext_fn_t *x,
                             const rs_value_t *args, uint8_t argc, rs_value_t *out)
{
    rs_arg_t in[RS_FUNC_ARGS_MAX];
    rs_arg_t r;
    uint8_t i;

    if (argc != x->argc) {
        rs_err0(env->err, RS_ERR_ARG_COUNT, "arg count mismatch");
        return RS_ERR_ARG_COUNT;
    }
    for (i = 0U; i < argc; i++) {
        memset(&in[i], 0, sizeof(in[i]));
        switch (args[i].type) {
        case RS_VAL_INT:
            in[i].type = RS_ARG_INT;
            in[i].i = args[i].i;
            break;
        case RS_VAL_FLOAT:
            in[i].type = RS_ARG_FLOAT;
            in[i].f = args[i].f;
            break;
        case RS_VAL_BOOL:
            in[i].type = RS_ARG_INT;
            in[i].i = args[i].b ? 1 : 0;
            break;
        default:
            return rs_type_err(env, "extension function needs numbers");
        }
    }
    r = x->fn(in, (uint32_t)argc);
    memset(out, 0, sizeof(*out));
    switch (r.type) {
    case RS_ARG_INT:
        out->type = RS_VAL_INT;
        out->i = r.i;
        return RS_OK;
    case RS_ARG_FLOAT:
        out->type = RS_VAL_FLOAT;
        out->f = r.f;
        return RS_OK;
    case RS_ARG_BOOL:
        out->type = RS_VAL_BOOL;
        out->b = r.b;
        return RS_OK;
    default:
        return rs_type_err(env, "extension function bad return");
    }
}

/* 数值比较：-1 / 0 / 1 */
static void rs_cmp_num(const rs_value_t *a, const rs_value_t *b, int *res)
{
    if (a->type == RS_VAL_INT && b->type == RS_VAL_INT) {
        *res = (a->i < b->i) ? -1 : ((a->i > b->i) ? 1 : 0);
    } else {
        float x = rs_val_fnum(a);
        float y = rs_val_fnum(b);
        *res = (x < y) ? -1 : ((x > y) ? 1 : 0);
    }
}

static int rs_binop(rs_env_t *env, const rs_tok_t *op, const rs_value_t *a,
                    const rs_value_t *b, rs_value_t *out)
{
    char c0 = op->p[0];
    rs_value_t va;
    rs_value_t vb;
    /* 调用方常以 rs_binop(op, out, &b, out) 传参：先快照再清零 out */
    va = *a;
    vb = *b;
    a = &va;
    b = &vb;
    memset(out, 0, sizeof(*out));
    if (op->len == 2U) {
        if (c0 == '&' || c0 == '|') {
            if (a->type != RS_VAL_BOOL || b->type != RS_VAL_BOOL) {
                return rs_type_err(env, "logical operands must be boolean");
            }
            out->type = RS_VAL_BOOL;
            out->b = (c0 == '&') ? (a->b && b->b) : (a->b || b->b);
            return RS_OK;
        }
        if (c0 == '+' || c0 == '-') {
            return rs_parse_err(env, "unknown statement");
        }
        /* == != <= >= */
        if (c0 == '=' || c0 == '!') {
            bool eq;
            if (a->type == RS_VAL_STRING || b->type == RS_VAL_STRING) {
                eq = (a->type == RS_VAL_STRING && b->type == RS_VAL_STRING &&
                      a->slen == b->slen &&
                      memcmp(a->s, b->s, a->slen) == 0);
            } else if (a->type == RS_VAL_BOOL || b->type == RS_VAL_BOOL) {
                if (a->type != RS_VAL_BOOL || b->type != RS_VAL_BOOL) {
                    return rs_type_err(env, "type mismatch");
                }
                eq = (a->b == b->b);
            } else {
                int r;
                rs_cmp_num(a, b, &r);
                eq = (r == 0);
            }
            out->type = RS_VAL_BOOL;
            out->b = (c0 == '=') ? eq : !eq;
            return RS_OK;
        }
        /* <= >= */
        {
            int r;
            if (!rs_val_is_num(a) || !rs_val_is_num(b)) {
                return rs_type_err(env, "comparison needs numbers");
            }
            rs_cmp_num(a, b, &r);
            out->type = RS_VAL_BOOL;
            out->b = (c0 == '<') ? (r <= 0) : (r >= 0);
            return RS_OK;
        }
    }
    switch (c0) {
    case '<': case '>': {
        int r;
        if (!rs_val_is_num(a) || !rs_val_is_num(b)) {
            return rs_type_err(env, "comparison needs numbers");
        }
        rs_cmp_num(a, b, &r);
        out->type = RS_VAL_BOOL;
        out->b = (c0 == '<') ? (r < 0) : (r > 0);
        return RS_OK;
    }
    case '+':
        if (a->type == RS_VAL_STRING || b->type == RS_VAL_STRING) {
            char fa[RS_STR_MAX];
            char fb[RS_STR_MAX];
            char cat[RS_STR_MAX * 2];
            uint16_t la = rs_fmt_value(a, fa, sizeof(fa));
            uint16_t lb = rs_fmt_value(b, fb, sizeof(fb));
            uint16_t len;
            if ((uint32_t)la + lb >= RS_STR_MAX) {
                rs_err0(env->err, RS_ERR_LIMIT, "string too long");
                return RS_ERR_LIMIT;
            }
            memcpy(cat, fa, la);
            memcpy(cat + la, fb, lb);
            len = (uint16_t)(la + lb);
            out->type = RS_VAL_STRING;
            out->s = rs_tmp(env->ctx, cat, len, &out->slen);
            return RS_OK;
        }
        if (!rs_val_is_num(a) || !rs_val_is_num(b)) {
            return rs_type_err(env, "type mismatch");
        }
        break;
    case '-':
        if (!rs_val_is_num(a) || !rs_val_is_num(b)) {
            return rs_type_err(env, "type mismatch");
        }
        break;
    case '*': case '/':
        if (!rs_val_is_num(a) || !rs_val_is_num(b)) {
            return rs_type_err(env, "type mismatch");
        }
        break;
    case '%':
        if (a->type != RS_VAL_INT || b->type != RS_VAL_INT) {
            return rs_type_err(env, "type mismatch");
        }
        if (b->i == 0) {
            rs_err0(env->err, RS_ERR_DIV_ZERO, "modulo by zero");
            return RS_ERR_DIV_ZERO;
        }
        out->type = RS_VAL_INT;
        out->i = a->i % b->i;
        return RS_OK;
    default:
        return rs_parse_err(env, "unknown statement");
    }
    /* 加 / 减 */
    if (a->type == RS_VAL_INT && b->type == RS_VAL_INT) {
        out->type = RS_VAL_INT;
        out->i = (c0 == '+') ? (a->i + b->i) : (a->i - b->i);
        return RS_OK;
    }
    out->type = RS_VAL_FLOAT;
    out->f = (c0 == '+') ? (rs_val_fnum(a) + rs_val_fnum(b))
                         : (rs_val_fnum(a) - rs_val_fnum(b));
    return RS_OK;
}

/* 乘 / 除（/ 单独处理：整除与除零 inf） */
static int rs_mulop(rs_env_t *env, const rs_tok_t *op, const rs_value_t *a,
                    const rs_value_t *b, rs_value_t *out)
{
    char c0 = op->p[0];
    rs_value_t va;
    rs_value_t vb;
    if (c0 == '%') {
        return rs_binop(env, op, a, b, out);
    }
    if (!rs_val_is_num(a) || !rs_val_is_num(b)) {
        return rs_type_err(env, "type mismatch");
    }
    /* 调用方常以 rs_mulop(op, out, &b, out) 传参：先快照再清零 out */
    va = *a;
    vb = *b;
    a = &va;
    b = &vb;
    memset(out, 0, sizeof(*out));
    if (c0 == '*') {
        if (a->type == RS_VAL_INT && b->type == RS_VAL_INT) {
            out->type = RS_VAL_INT;
            out->i = a->i * b->i;
        } else {
            out->type = RS_VAL_FLOAT;
            out->f = rs_val_fnum(a) * rs_val_fnum(b);
        }
        return RS_OK;
    }
    /* '/'：除零 -> 浮点 ±inf（规格 §3.2） */
    if (a->type == RS_VAL_INT && b->type == RS_VAL_INT) {
        if (b->i == 0) {
            out->type = RS_VAL_FLOAT;
            out->f = rs_inf(a->i < 0);
            return RS_OK;
        }
        out->type = RS_VAL_INT;
        out->i = a->i / b->i;
        return RS_OK;
    }
    if (rs_val_fnum(b) == 0.0f) {
        out->type = RS_VAL_FLOAT;
        out->f = rs_inf(rs_val_fnum(a) < 0.0f);
        return RS_OK;
    }
    out->type = RS_VAL_FLOAT;
    out->f = rs_val_fnum(a) / rs_val_fnum(b);
    return RS_OK;
}

static int rs_p_primary(rs_env_t *env, rs_ps_t *ps, rs_value_t *out)
{
    const rs_tok_t *t = &ps->t[ps->pos];
    memset(out, 0, sizeof(*out));
    switch (t->type) {
    case RS_TOK_NUMBER:
        ps->pos++;
        if (t->is_float) {
            out->type = RS_VAL_FLOAT;
            out->f = t->fval;
        } else {
            out->type = RS_VAL_INT;
            out->i = t->ival;
        }
        return RS_OK;
    case RS_TOK_STRING: {
        char sb[RS_STR_MAX];
        uint16_t len;
        ps->pos++;
        if (ps->noeval) {
            out->type = RS_VAL_INT;
            return RS_OK;
        }
        if (t->len > RS_STR_MAX + 1U) {
            return rs_parse_err(env, "string too long");
        }
        if (!rs_str_unescape(t, sb, sizeof(sb), &len)) {
            return rs_parse_err(env, "unclosed string");
        }
        out->type = RS_VAL_STRING;
        out->s = rs_tmp(env->ctx, sb, len, &out->slen);
        return RS_OK;
    }
    case RS_TOK_VAR: {
        rs_var_t *v;
        ps->pos++;
        if (ps->noeval) {
            out->type = RS_VAL_INT;
        } else {
            v = rs_var_find(env->ctx, t->p + 1, (uint16_t)(t->len - 1U));
            if (v == NULL) {
                rs_err0(env->err, RS_ERR_UNDEF_VAR, "undefined variable");
                return RS_ERR_UNDEF_VAR;
            }
            out->type = v->type;
            out->i = v->i;
            out->f = v->f;
            out->b = v->b;
            out->s = v->str;
            out->slen = v->slen;
        }
        /* 后缀 ++ / --：先取值后加减 */
        if (rs_op(&ps->t[ps->pos], "++") || rs_op(&ps->t[ps->pos], "--")) {
            bool inc = (ps->t[ps->pos].p[0] == '+');
            ps->pos++;
            if (!ps->noeval) {
                if (out->type != RS_VAL_INT && out->type != RS_VAL_FLOAT) {
                    return rs_type_err(env, "type mismatch");
                }
                v = rs_var_find(env->ctx, t->p + 1, (uint16_t)(t->len - 1U));
                if (v == NULL) {
                    rs_err0(env->err, RS_ERR_UNDEF_VAR, "undefined variable");
                    return RS_ERR_UNDEF_VAR;
                }
                if (v->type == RS_VAL_INT) {
                    v->i += inc ? 1 : -1;
                } else {
                    v->f += inc ? 1.0f : -1.0f;
                }
            }
        }
        return RS_OK;
    }
    case RS_TOK_WORD:
        if (rs_kw(t, "true") || rs_kw(t, "false")) {
            out->type = RS_VAL_BOOL;
            out->b = rs_kw(t, "true");
            ps->pos++;
            return RS_OK;
        }
        if (rs_op(&ps->t[ps->pos + 1], "(")) {
            rs_value_t args[RS_FUNC_ARGS_MAX];
            uint8_t argc = 0;
            const rs_fn_t *f;
            const rs_ext_fn_t *x;
            int rc;
#if RS_ENABLE_MATH
            const rs_func_t *m;

            /* 查找优先级：内置函数 -> 扩展函数 -> 脚本函数
             * （内置 / 扩展名在登记期已被拒绝，此处仅按序分派） */
            m = rs_func_find(t->p, t->len);
            x = (m != NULL) ? NULL : rs_ext_func_find(t->p, t->len);
            f = (m != NULL || x != NULL) ? NULL
                                         : rs_find_func(env->prog, t->p, t->len);
#else
            x = rs_ext_func_find(t->p, t->len);
            f = (x != NULL) ? NULL : rs_find_func(env->prog, t->p, t->len);
#endif
            ps->pos++;
            rc = rs_p_args(env, ps, args, &argc);
            if (rc != RS_OK) {
                return rc;
            }
            if (ps->noeval) {
                out->type = RS_VAL_INT;
                return RS_OK;
            }
#if RS_ENABLE_MATH
            if (m != NULL) {
                return rs_func_apply(env, m, args, argc, out);
            }
#endif
            if (x != NULL) {
                return rs_ext_func_apply(env, x, args, argc, out);
            }
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
                                (uint8_t)(env->depth + 1U), env->ctx, out,
                                env->err);
        }
        /* 裸标识符：内置常量 -> 扩展常量（$x 变量为另一空间，不在此查找） */
        {
            float cv;

            if (rs_find_const_num(t->p, t->len, &cv)) {
                out->type = RS_VAL_FLOAT;
                out->f = cv;
                ps->pos++;
                return RS_OK;
            }
        }
        return rs_parse_err(env, "unknown statement");
    case RS_TOK_OP:
        if (rs_op(t, "(")) {
            int rc;
            ps->pos++;
            rc = rs_p_expr(env, ps, out);
            if (rc != RS_OK) {
                return rc;
            }
            if (!rs_op(&ps->t[ps->pos], ")")) {
                return rs_parse_err(env, "missing )");
            }
            ps->pos++;
            return RS_OK;
        }
        return rs_parse_err(env, "unknown statement");
    default:
        return rs_parse_err(env, "unknown statement");
    }
}

static int rs_p_unary(rs_env_t *env, rs_ps_t *ps, rs_value_t *out)
{
    const rs_tok_t *t = &ps->t[ps->pos];
    if (rs_op(t, "!")) {
        int rc;
        ps->pos++;
        rc = rs_p_unary(env, ps, out);
        if (rc != RS_OK) {
            return rc;
        }
        if (ps->noeval) {
            return RS_OK;
        }
        if (out->type != RS_VAL_BOOL) {
            return rs_type_err(env, "! needs boolean");
        }
        out->b = !out->b;
        return RS_OK;
    }
    if (rs_op(t, "-") && t->len == 1U) {
        int rc;
        ps->pos++;
        rc = rs_p_unary(env, ps, out);
        if (rc != RS_OK) {
            return rc;
        }
        if (ps->noeval) {
            return RS_OK;
        }
        if (!rs_val_is_num(out)) {
            return rs_type_err(env, "type mismatch");
        }
        if (out->type == RS_VAL_INT) {
            out->i = -out->i;
        } else {
            out->f = -out->f;
        }
        return RS_OK;
    }
    if (rs_op(t, "++") || rs_op(t, "--")) {
        bool inc = (t->p[0] == '+');
        const rs_tok_t *vt;
        rs_var_t *v;
        ps->pos++;
        vt = &ps->t[ps->pos];
        if (vt->type != RS_TOK_VAR) {
            return rs_parse_err(env, "++/-- needs variable");
        }
        ps->pos++;
        if (ps->noeval) {
            memset(out, 0, sizeof(*out));
            out->type = RS_VAL_INT;
            return RS_OK;
        }
        v = rs_var_find(env->ctx, vt->p + 1, (uint16_t)(vt->len - 1U));
        if (v == NULL) {
            rs_err0(env->err, RS_ERR_UNDEF_VAR, "undefined variable");
            return RS_ERR_UNDEF_VAR;
        }
        if (v->type != RS_VAL_INT && v->type != RS_VAL_FLOAT) {
            return rs_type_err(env, "type mismatch");
        }
        if (v->type == RS_VAL_INT) {
            v->i += inc ? 1 : -1;
            out->type = RS_VAL_INT;
            out->i = v->i;
        } else {
            v->f += inc ? 1.0f : -1.0f;
            out->type = RS_VAL_FLOAT;
            out->f = v->f;
        }
        return RS_OK;
    }
    return rs_p_primary(env, ps, out);
}

static int rs_p_mul(rs_env_t *env, rs_ps_t *ps, rs_value_t *out)
{
    rs_value_t b;
    int rc = rs_p_unary(env, ps, out);
    while (rc == RS_OK && ps->t[ps->pos].type == RS_TOK_OP &&
           (ps->t[ps->pos].p[0] == '*' || ps->t[ps->pos].p[0] == '/' ||
            ps->t[ps->pos].p[0] == '%') && ps->t[ps->pos].len == 1U) {
        const rs_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        rc = rs_p_unary(env, ps, &b);
        if (rc != RS_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = rs_mulop(env, op, out, &b, out);
    }
    return rc;
}

static int rs_p_add(rs_env_t *env, rs_ps_t *ps, rs_value_t *out)
{
    rs_value_t b;
    int rc = rs_p_mul(env, ps, out);
    while (rc == RS_OK && ps->t[ps->pos].type == RS_TOK_OP &&
           (ps->t[ps->pos].p[0] == '+' || ps->t[ps->pos].p[0] == '-') &&
           ps->t[ps->pos].len == 1U) {
        const rs_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        rc = rs_p_mul(env, ps, &b);
        if (rc != RS_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = rs_binop(env, op, out, &b, out);
    }
    return rc;
}

static int rs_p_rel(rs_env_t *env, rs_ps_t *ps, rs_value_t *out)
{
    rs_value_t b;
    int rc = rs_p_add(env, ps, out);
    /* < <= > >=（规格 §3.2 关系运算） */
    while (rc == RS_OK && ps->t[ps->pos].type == RS_TOK_OP &&
           (ps->t[ps->pos].p[0] == '<' || ps->t[ps->pos].p[0] == '>') &&
           ps->t[ps->pos].len <= 2U) {
        const rs_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        rc = rs_p_add(env, ps, &b);
        if (rc != RS_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = rs_binop(env, op, out, &b, out);
    }
    return rc;
}

static int rs_p_eq(rs_env_t *env, rs_ps_t *ps, rs_value_t *out)
{
    rs_value_t b;
    int rc = rs_p_rel(env, ps, out);
    while (rc == RS_OK && ps->t[ps->pos].type == RS_TOK_OP &&
           (ps->t[ps->pos].p[0] == '=' || ps->t[ps->pos].p[0] == '!') &&
           ps->t[ps->pos].len == 2U) {
        const rs_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        rc = rs_p_rel(env, ps, &b);
        if (rc != RS_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = rs_binop(env, op, out, &b, out);
    }
    return rc;
}

static int rs_p_and(rs_env_t *env, rs_ps_t *ps, rs_value_t *out)
{
    rs_value_t b;
    int rc = rs_p_eq(env, ps, out);
    while (rc == RS_OK && rs_op(&ps->t[ps->pos], "&&")) {
        const rs_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        if (!ps->noeval && out->type == RS_VAL_BOOL && !out->b) {
            rs_value_t dummy;
            ps->noeval = true;
            rc = rs_p_eq(env, ps, &dummy);
            ps->noeval = false;
            if (rc != RS_OK) {
                return rc;
            }
            continue;                       /* 短路：保持 false */
        }
        rc = rs_p_eq(env, ps, &b);
        if (rc != RS_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = rs_binop(env, op, out, &b, out);
    }
    return rc;
}

static int rs_p_or(rs_env_t *env, rs_ps_t *ps, rs_value_t *out)
{
    rs_value_t b;
    int rc = rs_p_and(env, ps, out);
    while (rc == RS_OK && rs_op(&ps->t[ps->pos], "||")) {
        const rs_tok_t *op = &ps->t[ps->pos];
        ps->pos++;
        if (!ps->noeval && out->type == RS_VAL_BOOL && out->b) {
            rs_value_t dummy;
            ps->noeval = true;
            rc = rs_p_and(env, ps, &dummy);
            ps->noeval = false;
            if (rc != RS_OK) {
                return rc;
            }
            continue;
        }
        rc = rs_p_and(env, ps, &b);
        if (rc != RS_OK) {
            return rc;
        }
        if (ps->noeval) {
            continue;
        }
        rc = rs_binop(env, op, out, &b, out);
    }
    return rc;
}

static int rs_p_expr(rs_env_t *env, rs_ps_t *ps, rs_value_t *out)
{
    return rs_p_or(env, ps, out);
}

/* 实参列表：ps->pos 指向 '(' */
int rs_p_args(rs_env_t *env, rs_ps_t *ps, rs_value_t *args, uint8_t *argc)
{
    int rc;
    *argc = 0;
    ps->pos++;
    if (rs_op(&ps->t[ps->pos], ")")) {
        ps->pos++;
        return RS_OK;
    }
    for (;;) {
        if (*argc >= RS_FUNC_ARGS_MAX) {
            return rs_err0(env->err, RS_ERR_LIMIT, "too many arguments"),
                   RS_ERR_LIMIT;
        }
        rc = rs_p_expr(env, ps, &args[*argc]);
        if (rc != RS_OK) {
            return rc;
        }
        (*argc)++;
        if (rs_op(&ps->t[ps->pos], ",")) {
            ps->pos++;
            continue;
        }
        if (rs_op(&ps->t[ps->pos], ")")) {
            ps->pos++;
            return RS_OK;
        }
        return rs_parse_err(env, "bad argument list");
    }
}

/* 求值 toks[start..] 并要求全部消费 */
int rs_eval_tokens(rs_env_t *env, const rs_tok_t *toks, int n, int start,
                          rs_value_t *out)
{
    rs_ps_t ps;
    int rc;
    ps.t = toks;
    ps.n = n;
    ps.pos = start;
    ps.noeval = false;
    rc = rs_p_expr(env, &ps, out);
    if (rc != RS_OK) {
        return rc;
    }
    if (ps.pos != n - 1) {
        return rs_parse_err(env, "syntax error");
    }
    return RS_OK;
}

int rs_eval_cond(rs_env_t *env, const rs_tok_t *t, int tn, int start,
                        bool *out)
{
    rs_value_t v;
    int rc = rs_eval_tokens(env, t, tn, start, &v);
    if (rc != RS_OK) {
        return rc;
    }
    if (v.type != RS_VAL_BOOL) {
        return rs_type_err(env, "condition must be boolean");
    }
    *out = v.b;
    return RS_OK;
}

#endif /* RS_ENABLE_EXPR */
