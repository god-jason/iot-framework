/*
 * shell_expr.c - 词法分析、变量表与表达式求值
 *
 * 词法：剥离注释（字符串外、token 边界），识别字符串 / 数值（整数、小数、
 *      科学计数法，不带单位后缀）/ 变量 / 运算符 / 标识符；
 *      标识符比较统一折叠大小写。
 * 表达式：递归下降，优先级与 V1.1 §5.3 一致，AND/OR 短路求值。
 */

#include "shell_internal.h"

#include <string.h>
#include <stdio.h>

/* ------------------------------ 小工具 ------------------------------ */

static char shell_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool shell_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool shell_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool shell_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* 大小写不敏感比较（a 为定长片段，b 为 C 字符串） */
bool shell_ieq(const char *a, uint16_t alen, const char *b)
{
    uint16_t i = 0U;

    if (b == NULL) {
        return false;
    }
    for (i = 0U; i < alen; i++) {
        if (b[i] == '\0' || shell_upper(a[i]) != shell_upper(b[i])) {
            return false;
        }
    }
    return b[alen] == '\0';
}

bool shell_tok_ieq(const shell_tok_t *t, const char *s)
{
    return (t != NULL) && (t->type != SHELL_TOK_EOF) &&
           shell_ieq(t->p, t->len, s);
}

/* ------------------------------ 词法 ------------------------------ */

/*
 * 把一行文本切成 token。
 * 返回 token 数（含 EOF）；token 的 p/len 指向 text，调用方需保证其生命周期。
 */
int shell_lex(const char *text, uint16_t len, shell_tok_t *toks, uint16_t max)
{
    uint16_t i = 0U;
    uint16_t n = 0U;

    if (text == NULL || toks == NULL || max < 2U) {
        return 0;
    }

    while (i < len && n + 1U < max) {
        shell_tok_t *t = &toks[n];
        char c = text[i];

        if (shell_is_space(c) || c == ',') {
            i++;
            continue;
        }
        /* 注释：token 边界上的 ; 或 // */
        if (c == ';') {
            break;
        }
        if (c == '/' && i + 1U < len && text[i + 1U] == '/') {
            break;
        }

        memset(t, 0, sizeof(*t));
        t->p = &text[i];

        if (c == '"') {
            uint16_t quote = i;
            uint16_t start = ++i;

            while (i < len) {
                if (text[i] == '\\' && i + 1U < len && text[i + 1U] == '"') {
                    i += 2U;
                    continue;
                }
                if (text[i] == '"') {
                    break;
                }
                i++;
            }
            t->type = SHELL_TOK_STRING;
            t->p = &text[quote];        /* 含引号，便于参数切片保持原样 */
            t->len = (uint16_t)(i - quote);
            if (i < len) {
                t->len++;               /* 含右引号 */
                i++;
            } else {
                t->len = 1U;            /* 未闭合：只留开引号，求值时报错 */
            }
            (void)start;
            n++;
            continue;
        }

        if (c == '$') {
            uint16_t start = i;

            i++;
            while (i < len && (shell_is_alpha(text[i]) || shell_is_digit(text[i]))) {
                i++;
            }
            t->type = SHELL_TOK_VAR;
            t->p = &text[start];        /* 含 $，便于参数切片保持原样 */
            t->len = (uint16_t)(i - start);
            n++;
            continue;
        }

        if (shell_is_digit(c) || (c == '.' && i + 1U < len && shell_is_digit(text[i + 1U]))) {
            uint16_t start = i;
            bool is_float = false;

            while (i < len && shell_is_digit(text[i])) {
                i++;
            }
            if (i < len && text[i] == '.') {
                is_float = true;
                i++;
                while (i < len && shell_is_digit(text[i])) {
                    i++;
                }
            }
            if (i < len && (text[i] == 'e' || text[i] == 'E')) {
                uint16_t save = i;

                i++;
                if (i < len && (text[i] == '+' || text[i] == '-')) {
                    i++;
                }
                if (i < len && shell_is_digit(text[i])) {
                    is_float = true;
                    while (i < len && shell_is_digit(text[i])) {
                        i++;
                    }
                } else {
                    i = save;   /* 不是指数，回退 */
                }
            }

            t->type = SHELL_TOK_NUMBER;
            t->p = &text[start];
            t->len = (uint16_t)(i - start);

            /* 数值转换：此处不依赖 strtof/atoi，保持裸机可用 */
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
                        break;      /* 指数部分由下方 strtof 兜底 */
                    } else if (d >= '0' && d <= '9') {
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

                /* 指数部分：1e3 / 2.5e-2 */
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
                        while (k < t->len && t->p[k] >= '0' && t->p[k] <= '9') {
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

        if (shell_is_alpha(c)) {
            uint16_t start = i;

            while (i < len && (shell_is_alpha(text[i]) || shell_is_digit(text[i]) ||
                               text[i] == '.')) {
                i++;
            }
            t->type = SHELL_TOK_WORD;
            t->p = &text[start];
            t->len = (uint16_t)(i - start);
            n++;
            continue;
        }

        /* 运算符：两字符优先 */
        t->type = SHELL_TOK_OP;
        if (i + 1U < len) {
            const char *two = &text[i];
            if ((two[0] == '=' && two[1] == '=') ||
                (two[0] == '!' && two[1] == '=') ||
                (two[0] == '<' && two[1] == '=') ||
                (two[0] == '>' && two[1] == '=') ||
                (two[0] == '&' && two[1] == '&') ||
                (two[0] == '|' && two[1] == '|')) {
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
    toks[n].type = SHELL_TOK_EOF;
    return (int)(n + 1U);
}

/* ------------------------------ 变量表 ------------------------------ */

int shell_vars_init(shell_vars_t *vars)
{
    if (vars == NULL) {
        return -1;
    }
    memset(vars, 0, sizeof(*vars));
    return 0;
}

void shell_vars_clear(shell_vars_t *vars)
{
    if (vars != NULL) {
        memset(vars, 0, sizeof(*vars));
    }
}

shell_var_t *shell_var_find(shell_vars_t *vars, const char *name, uint16_t len)
{
    uint16_t i;

    if (vars == NULL || name == NULL || len == 0U ||
        len >= SHELL_VAR_NAME_MAX) {
        return NULL;
    }
    for (i = 0U; i < SHELL_VAR_MAX; i++) {
        shell_var_t *var = &vars->items[i];
        uint16_t k;
        bool same;

        if (!var->used || (uint16_t)strlen(var->name) != len) {
            continue;
        }
        same = true;
        for (k = 0U; k < len; k++) {
            if (shell_upper(var->name[k]) != shell_upper(name[k])) {
                same = false;
                break;
            }
        }
        if (same) {
            return var;
        }
    }
    return NULL;
}

static int shell_var_alloc(shell_vars_t *vars, const char *name, uint16_t len)
{
    uint16_t i;

    for (i = 0U; i < SHELL_VAR_MAX; i++) {
        if (!vars->items[i].used) {
            uint16_t k;

            memset(&vars->items[i], 0, sizeof(vars->items[i]));
            for (k = 0U; k < len && k + 1U < SHELL_VAR_NAME_MAX; k++) {
                vars->items[i].name[k] = shell_upper(name[k]);
            }
            vars->items[i].used = true;
            return (int)i;
        }
    }
    return -1;
}

/* 把值写入变量槽位（不检查 readonly，供只读内建变量内部更新） */
static void shell_var_store(shell_var_t *var, const shell_value_t *val)
{
    var->type = val->type;
    switch (val->type) {
    case SHELL_VAL_INT:
        var->i = val->i;
        break;
    case SHELL_VAL_FLOAT:
        var->f = val->f;
        break;
    case SHELL_VAL_BOOL:
        var->b = val->b;
        break;
    case SHELL_VAL_STRING: {
        uint16_t n = (val->slen < SHELL_STR_MAX - 1U) ? val->slen
                                                      : (uint16_t)(SHELL_STR_MAX - 1U);
        uint16_t i;
        uint16_t k = 0U;

        for (i = 0U; i < n && k + 1U < SHELL_STR_MAX; i++) {
            if (val->s[i] == '\\' && i + 1U < n && val->s[i + 1U] == '"') {
                i++;
            }
            var->str[k++] = val->s[i];
        }
        var->str[k] = '\0';
        var->slen = k;
        break;
    }
    default:
        break;
    }
}

int shell_vars_set(shell_vars_t *vars, const char *name, uint16_t nlen,
                   const shell_value_t *val, bool readonly, shell_err_t *err)
{
    shell_var_t *var;

    if (vars == NULL || name == NULL || val == NULL || nlen == 0U) {
        if (err != NULL) {
            err->code = SHELL_ERR_PARAM;
            snprintf(err->msg, sizeof(err->msg), "invalid variable");
        }
        return -1;
    }

    var = shell_var_find(vars, name, nlen);
    if (var == NULL) {
        if (shell_var_alloc(vars, name, nlen) < 0) {
            if (err != NULL) {
                err->code = SHELL_ERR_RESOURCE;
                snprintf(err->msg, sizeof(err->msg), "variable table full");
            }
            return -1;
        }
        var = shell_var_find(vars, name, nlen);
    }
    if (var->used && var->readonly) {
        if (err != NULL) {
            err->code = SHELL_ERR_READONLY;
            snprintf(err->msg, sizeof(err->msg), "variable $%.*s is read-only",
                     (int)nlen, name);
        }
        return -1;
    }
    var->readonly = readonly;
    shell_var_store(var, val);
    return 0;
}

int shell_vars_get(shell_vars_t *vars, const char *name, uint16_t nlen,
                   shell_value_t *out, shell_err_t *err)
{
    shell_var_t *var = shell_var_find(vars, name, nlen);

    if (var == NULL) {
        if (err != NULL) {
            err->code = SHELL_ERR_UNDEF_VAR;
            snprintf(err->msg, sizeof(err->msg), "undefined variable $%.*s",
                     (int)nlen, name);
        }
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->type = var->type;
    switch (var->type) {
    case SHELL_VAL_INT:    out->i = var->i; break;
    case SHELL_VAL_FLOAT:  out->f = var->f; break;
    case SHELL_VAL_BOOL:   out->b = var->b; break;
    case SHELL_VAL_STRING:
        out->s = var->str;
        out->slen = var->slen;
        break;
    default:
        break;
    }
    return 0;
}

int shell_vars_unset(shell_vars_t *vars, const char *name, uint16_t nlen)
{
    shell_var_t *var = shell_var_find(vars, name, nlen);

    if (var == NULL) {
        return -1;
    }
    if (var->readonly) {
        return -2;
    }
    memset(var, 0, sizeof(*var));
    return 0;
}

/* ------------------------------ 值转换 ------------------------------ */

int shell_value_to_int(const shell_value_t *v, int32_t *out, shell_err_t *err)
{
    switch (v->type) {
    case SHELL_VAL_INT:
        *out = v->i;
        return 0;
    case SHELL_VAL_FLOAT:
        *out = (int32_t)v->f;
        return 0;
    case SHELL_VAL_BOOL:
        *out = v->b ? 1 : 0;
        return 0;
    default:
        if (err != NULL) {
            err->code = SHELL_ERR_TYPE;
            snprintf(err->msg, sizeof(err->msg), "type mismatch");
        }
        return -1;
    }
}

void shell_value_format(const shell_value_t *v, char *buf, uint16_t cap,
                        uint16_t *len)
{
    int n = 0;

    switch (v->type) {
    case SHELL_VAL_INT:
        n = snprintf(buf, cap, "%ld", (long)v->i);
        break;
    case SHELL_VAL_FLOAT:
        n = snprintf(buf, cap, "%g", (double)v->f);
        break;
    case SHELL_VAL_BOOL:
        n = snprintf(buf, cap, "%s", v->b ? "TRUE" : "FALSE");
        break;
    case SHELL_VAL_STRING: {
        uint16_t c = (v->slen < cap - 1U) ? v->slen : (uint16_t)(cap - 1U);

        memcpy(buf, v->s, c);
        buf[c] = '\0';
        n = (int)c;
        break;
    }
    default:
        buf[0] = '\0';
        n = 0;
        break;
    }
    if (len != NULL) {
        *len = (n > 0) ? (uint16_t)n : 0U;
    }
}

/* ------------------------------ 表达式 ------------------------------ */

typedef struct {
    const shell_tok_t *toks;
    uint16_t count;
    uint16_t pos;
    shell_vars_t *vars;
    shell_err_t *err;
} shell_expr_ctx_t;

static const shell_tok_t *shell_peek(shell_expr_ctx_t *c)
{
    return &c->toks[c->pos];
}

static const shell_tok_t *shell_next(shell_expr_ctx_t *c)
{
    if (c->pos < c->count) {
        return &c->toks[c->pos++];
    }
    return &c->toks[c->count - 1U];
}

static int shell_expr_or(shell_expr_ctx_t *c, bool eval, shell_value_t *out);

static int shell_expr_fail(shell_expr_ctx_t *c, int code, const char *msg)
{
    if (c->err != NULL) {
        c->err->code = code;
        snprintf(c->err->msg, sizeof(c->err->msg), "%s", msg);
    }
    return -1;
}

/* 按 0=假、非 0=真 解释；字符串报类型错误 */
static int shell_value_truth(const shell_value_t *v, bool *out, shell_expr_ctx_t *c)
{
    switch (v->type) {
    case SHELL_VAL_BOOL:
        *out = v->b;
        return 0;
    case SHELL_VAL_INT:
        *out = (v->i != 0);
        return 0;
    case SHELL_VAL_FLOAT:
        *out = (v->f != 0.0f);
        return 0;
    default:
        return shell_expr_fail(c, SHELL_ERR_TYPE, "type mismatch");
    }
}

static bool shell_need_number(const shell_value_t *v)
{
    return v->type == SHELL_VAL_INT || v->type == SHELL_VAL_FLOAT;
}

/* 比较用的数值视图：布尔按 0/1 参与比较 */
static float shell_value_number(const shell_value_t *v)
{
    switch (v->type) {
    case SHELL_VAL_FLOAT: return v->f;
    case SHELL_VAL_BOOL:  return v->b ? 1.0f : 0.0f;
    default:              return (float)v->i;
    }
}

static int shell_expr_primary(shell_expr_ctx_t *c, bool eval, shell_value_t *out)
{
    const shell_tok_t *t = shell_next(c);

    memset(out, 0, sizeof(*out));

    if (t->type == SHELL_TOK_NUMBER) {
        if (t->is_float) {
            out->type = SHELL_VAL_FLOAT;
            out->f = t->fval;
        } else {
            out->type = SHELL_VAL_INT;
            out->i = t->ival;
        }
        return 0;
    }
    if (t->type == SHELL_TOK_STRING) {
        if (t->len < 2U) {
            return shell_expr_fail(c, SHELL_ERR_PARSE, "unterminated string");
        }
        out->type = SHELL_VAL_STRING;
        out->s = t->p + 1;
        out->slen = (uint16_t)(t->len - 2U);
        return 0;
    }
    if (t->type == SHELL_TOK_VAR) {
        if (!eval) {
            out->type = SHELL_VAL_INT;
            return 0;
        }
        if (t->len <= 1U) {
            return shell_expr_fail(c, SHELL_ERR_PARSE, "invalid variable");
        }
        return shell_vars_get(c->vars, t->p + 1, (uint16_t)(t->len - 1U),
                              out, c->err);
    }
    if (t->type == SHELL_TOK_WORD) {
        if (shell_tok_ieq(t, "TRUE") || shell_tok_ieq(t, "ON")) {
            out->type = SHELL_VAL_BOOL;
            out->b = true;
            return 0;
        }
        if (shell_tok_ieq(t, "FALSE") || shell_tok_ieq(t, "OFF")) {
            out->type = SHELL_VAL_BOOL;
            out->b = false;
            return 0;
        }
        return shell_expr_fail(c, SHELL_ERR_PARSE, "unexpected identifier");
    }
    if (t->type == SHELL_TOK_OP && t->len == 1U && t->p[0] == '(') {
        if (shell_expr_or(c, eval, out) != 0) {
            return -1;
        }
        t = shell_next(c);
        if (!(t->type == SHELL_TOK_OP && t->len == 1U && t->p[0] == ')')) {
            return shell_expr_fail(c, SHELL_ERR_PARSE, "unmatched parenthesis");
        }
        return 0;
    }
    return shell_expr_fail(c, SHELL_ERR_PARSE, "unexpected token");
}

static int shell_expr_unary(shell_expr_ctx_t *c, bool eval, shell_value_t *out)
{
    const shell_tok_t *t = shell_peek(c);

    if (shell_tok_ieq(t, "NOT") ||
        (t->type == SHELL_TOK_OP && t->len == 1U && t->p[0] == '!')) {
        bool b;

        shell_next(c);
        if (shell_expr_unary(c, eval, out) != 0) {
            return -1;
        }
        if (!eval) {
            out->type = SHELL_VAL_BOOL;
            return 0;
        }
        if (shell_value_truth(out, &b, c) != 0) {
            return -1;
        }
        out->type = SHELL_VAL_BOOL;
        out->b = !b;
        return 0;
    }
    if (t->type == SHELL_TOK_OP && t->len == 1U && t->p[0] == '-') {
        shell_next(c);
        if (shell_expr_unary(c, eval, out) != 0) {
            return -1;
        }
        if (!eval) {
            return 0;
        }
        if (!shell_need_number(out)) {
            return shell_expr_fail(c, SHELL_ERR_TYPE, "type mismatch");
        }
        if (out->type == SHELL_VAL_INT) {
            out->i = -out->i;
        } else {
            out->f = -out->f;
        }
        return 0;
    }
    return shell_expr_primary(c, eval, out);
}

static int shell_expr_mul(shell_expr_ctx_t *c, bool eval, shell_value_t *out)
{
    shell_value_t rhs;

    if (shell_expr_unary(c, eval, out) != 0) {
        return -1;
    }
    for (;;) {
        const shell_tok_t *t = shell_peek(c);
        char op;

        if (t->type != SHELL_TOK_OP || t->len != 1U ||
            (t->p[0] != '*' && t->p[0] != '/' && t->p[0] != '%')) {
            return 0;
        }
        op = t->p[0];
        shell_next(c);
        if (shell_expr_unary(c, eval, &rhs) != 0) {
            return -1;
        }
        if (!eval) {
            continue;
        }
        if (!shell_need_number(out) || !shell_need_number(&rhs)) {
            return shell_expr_fail(c, SHELL_ERR_TYPE, "type mismatch");
        }
        if (op == '%') {
            int32_t a;
            int32_t b;

            if (out->type != SHELL_VAL_INT || rhs.type != SHELL_VAL_INT) {
                return shell_expr_fail(c, SHELL_ERR_TYPE, "type mismatch");
            }
            a = out->i;
            b = rhs.i;
            if (b == 0) {
                return shell_expr_fail(c, SHELL_ERR_DIV_ZERO, "modulo by zero");
            }
            out->i = a % b;
            continue;
        }
        if (out->type == SHELL_VAL_FLOAT || rhs.type == SHELL_VAL_FLOAT) {
            float a = (out->type == SHELL_VAL_FLOAT) ? out->f : (float)out->i;
            float b = (rhs.type == SHELL_VAL_FLOAT) ? rhs.f : (float)rhs.i;

            if (op == '/' && b == 0.0f) {
                return shell_expr_fail(c, SHELL_ERR_DIV_ZERO, "divide by zero");
            }
            out->type = SHELL_VAL_FLOAT;
            out->f = (op == '*') ? (a * b) : (a / b);
        } else {
            if (op == '/' && rhs.i == 0) {
                return shell_expr_fail(c, SHELL_ERR_DIV_ZERO, "divide by zero");
            }
            if (op == '*') {
                out->i = out->i * rhs.i;
            } else {
                out->i = out->i / rhs.i;
            }
            out->type = SHELL_VAL_INT;
        }
    }
}

static int shell_expr_add(shell_expr_ctx_t *c, bool eval, shell_value_t *out)
{
    shell_value_t rhs;

    if (shell_expr_mul(c, eval, out) != 0) {
        return -1;
    }
    for (;;) {
        const shell_tok_t *t = shell_peek(c);
        char op;

        if (t->type != SHELL_TOK_OP || t->len != 1U ||
            (t->p[0] != '+' && t->p[0] != '-')) {
            return 0;
        }
        op = t->p[0];
        shell_next(c);
        if (shell_expr_mul(c, eval, &rhs) != 0) {
            return -1;
        }
        if (!eval) {
            continue;
        }
        if (!shell_need_number(out) || !shell_need_number(&rhs)) {
            return shell_expr_fail(c, SHELL_ERR_TYPE, "type mismatch");
        }
        if (out->type == SHELL_VAL_FLOAT || rhs.type == SHELL_VAL_FLOAT) {
            float a = (out->type == SHELL_VAL_FLOAT) ? out->f : (float)out->i;
            float b = (rhs.type == SHELL_VAL_FLOAT) ? rhs.f : (float)rhs.i;

            out->type = SHELL_VAL_FLOAT;
            out->f = (op == '+') ? (a + b) : (a - b);
        } else {
            out->i = (op == '+') ? (out->i + rhs.i) : (out->i - rhs.i);
            out->type = SHELL_VAL_INT;
        }
    }
}

static int shell_expr_rel(shell_expr_ctx_t *c, bool eval, shell_value_t *out)
{
    shell_value_t rhs;

    if (shell_expr_add(c, eval, out) != 0) {
        return -1;
    }
    for (;;) {
        const shell_tok_t *t = shell_peek(c);
        uint8_t op = 0U;

        if (t->type == SHELL_TOK_OP && t->len == 1U &&
            (t->p[0] == '<' || t->p[0] == '>')) {
            op = (t->p[0] == '<') ? 0U : 2U;   /* 0: <, 2: > */
        } else if (t->type == SHELL_TOK_OP && t->len == 2U &&
                   (t->p[0] == '<' || t->p[0] == '>') && t->p[1] == '=') {
            op = (t->p[0] == '<') ? 1U : 3U;   /* 1: <=, 3: >= */
        } else {
            return 0;
        }
        shell_next(c);
        if (shell_expr_add(c, eval, &rhs) != 0) {
            return -1;
        }
        if (!eval) {
            continue;
        }
        if (!shell_need_number(out) || !shell_need_number(&rhs)) {
            return shell_expr_fail(c, SHELL_ERR_TYPE, "type mismatch");
        }
        {
            float a = shell_value_number(out);
            float b = shell_value_number(&rhs);
            bool r;

            switch (op) {
            case 1U:  r = (a <= b); break;
            case 2U:  r = (a > b); break;
            case 3U:  r = (a >= b); break;
            default:  r = (a < b); break;
            }
            out->type = SHELL_VAL_BOOL;
            out->b = r;
        }
    }
}

static int shell_expr_eq(shell_expr_ctx_t *c, bool eval, shell_value_t *out)
{
    shell_value_t rhs;

    if (shell_expr_rel(c, eval, out) != 0) {
        return -1;
    }
    for (;;) {
        const shell_tok_t *t = shell_peek(c);
        bool eq;

        if (!(t->type == SHELL_TOK_OP && t->len == 2U &&
              (t->p[0] == '=' || t->p[0] == '!') && t->p[1] == '=')) {
            return 0;
        }
        eq = (t->p[0] == '=');
        shell_next(c);
        if (shell_expr_rel(c, eval, &rhs) != 0) {
            return -1;
        }
        if (!eval) {
            continue;
        }
        if (out->type == SHELL_VAL_STRING || rhs.type == SHELL_VAL_STRING) {
            bool same;

            if (out->type != SHELL_VAL_STRING || rhs.type != SHELL_VAL_STRING) {
                return shell_expr_fail(c, SHELL_ERR_TYPE, "type mismatch");
            }
            same = (out->slen == rhs.slen) &&
                   (memcmp(out->s, rhs.s, out->slen) == 0);
            out->type = SHELL_VAL_BOOL;
            out->b = eq ? same : !same;
        } else {
            float a = shell_value_number(out);
            float b = shell_value_number(&rhs);
            bool same = (a == b);

            out->type = SHELL_VAL_BOOL;
            out->b = eq ? same : !same;
        }
    }
}

static int shell_expr_and(shell_expr_ctx_t *c, bool eval, shell_value_t *out)
{
    shell_value_t rhs;

    if (shell_expr_eq(c, eval, out) != 0) {
        return -1;
    }
    for (;;) {
        const shell_tok_t *t = shell_peek(c);
        bool left_true = false;
        bool eval_rhs;

        if (!(shell_tok_ieq(t, "AND") ||
              (t->type == SHELL_TOK_OP && t->len == 2U && t->p[0] == '&'))) {
            return 0;
        }
        shell_next(c);
        eval_rhs = eval;
        if (eval) {
            if (shell_value_truth(out, &left_true, c) != 0) {
                return -1;
            }
            if (!left_true) {
                eval_rhs = false;       /* 短路：右侧不求值 */
            }
        }
        if (shell_expr_eq(c, eval_rhs, &rhs) != 0) {
            return -1;
        }
        if (!eval) {
            out->type = SHELL_VAL_BOOL;
            continue;
        }
        if (left_true) {
            bool right_true;

            if (shell_value_truth(&rhs, &right_true, c) != 0) {
                return -1;
            }
            out->b = right_true;
        } else {
            out->b = false;
        }
        out->type = SHELL_VAL_BOOL;
    }
}

static int shell_expr_or(shell_expr_ctx_t *c, bool eval, shell_value_t *out)
{
    shell_value_t rhs;

    if (shell_expr_and(c, eval, out) != 0) {
        return -1;
    }
    for (;;) {
        const shell_tok_t *t = shell_peek(c);
        bool left_true = false;
        bool eval_rhs;

        if (!(shell_tok_ieq(t, "OR") ||
              (t->type == SHELL_TOK_OP && t->len == 2U && t->p[0] == '|'))) {
            return 0;
        }
        shell_next(c);
        eval_rhs = eval;
        if (eval) {
            if (shell_value_truth(out, &left_true, c) != 0) {
                return -1;
            }
            if (left_true) {
                eval_rhs = false;       /* 短路：右侧不求值 */
            }
        }
        if (shell_expr_and(c, eval_rhs, &rhs) != 0) {
            return -1;
        }
        if (!eval) {
            out->type = SHELL_VAL_BOOL;
            continue;
        }
        if (left_true) {
            out->b = true;
        } else {
            bool right_true;

            if (shell_value_truth(&rhs, &right_true, c) != 0) {
                return -1;
            }
            out->b = right_true;
        }
        out->type = SHELL_VAL_BOOL;
    }
}

/*
 * 对 token 序列求值；必须消费完全部 token（EOF 除外）。
 * 返回 0 成功，-1 失败（err 中带错误码与消息）。
 */
int shell_expr_eval(const shell_tok_t *toks, uint16_t count,
                    shell_vars_t *vars, shell_value_t *out, shell_err_t *err)
{
    shell_expr_ctx_t c;

    if (toks == NULL || count == 0U || out == NULL) {
        return -1;
    }
    c.toks = toks;
    c.count = count;
    c.pos = 0U;
    c.vars = vars;
    c.err = err;

    memset(out, 0, sizeof(*out));
    if (shell_expr_or(&c, true, out) != 0) {
        return -1;
    }
    if (shell_peek(&c)->type != SHELL_TOK_EOF) {
        return shell_expr_fail(&c, SHELL_ERR_PARSE, "unexpected token");
    }
    return 0;
}
