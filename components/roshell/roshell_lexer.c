/*
 * roshell_lexer.c - roshell 词法分析
 *
 * 单行就地词法（规格 §2）：token 切分、字符分类、关键字 / 运算符匹配、
 * 字符串去转义与行首关键字分类。token 数组由调用方（加载扫描 / 求值 /
 * 语句解释）提供，本文件不持有状态。
 */

#include "roshell.h"
#include "roshell_lexer.h"

#include <string.h>

bool rs_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool rs_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool rs_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* 关键字/单词精确匹配（大小写敏感，规格 §2） */
bool rs_kw(const rs_tok_t *t, const char *s)
{
    uint16_t n;
    if (t == NULL || t->type != RS_TOK_WORD) {
        return false;
    }
    n = (uint16_t)strlen(s);
    return t->len == n && memcmp(t->p, s, n) == 0;
}

/* 运算符 token 匹配 */
bool rs_op(const rs_tok_t *t, const char *s)
{
    uint16_t n;
    if (t == NULL || t->type != RS_TOK_OP) {
        return false;
    }
    n = (uint16_t)strlen(s);
    return t->len == n && memcmp(t->p, s, n) == 0;
}
/* ============================== 词法 ============================== */

int rs_lex(const char *text, rs_tok_t *toks, uint16_t max)
{
    uint16_t i = 0U;
    uint16_t n = 0U;
    if (text == NULL || toks == NULL || max < 2U) {
        return 0;
    }
    while (text[i] != '\0' && n + 1U < max) {
        rs_tok_t *t = &toks[n];
        char c = text[i];
        if (rs_is_space(c)) {
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
            t->type = RS_TOK_STRING;
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
            while (rs_is_alpha(text[i]) || rs_is_digit(text[i])) {
                i++;
            }
            t->type = RS_TOK_VAR;
            t->p = &text[start];
            t->len = (uint16_t)(i - start);
            n++;
            continue;
        }
        if (rs_is_digit(c) || (c == '.' && rs_is_digit(text[i + 1U]))) {
            uint16_t start = i;
            bool is_float = false;
            while (rs_is_digit(text[i])) {
                i++;
            }
            if (text[i] == '.') {
                is_float = true;
                i++;
                while (rs_is_digit(text[i])) {
                    i++;
                }
            }
            if (text[i] == 'e' || text[i] == 'E') {
                uint16_t save = i;
                i++;
                if (text[i] == '+' || text[i] == '-') {
                    i++;
                }
                if (rs_is_digit(text[i])) {
                    is_float = true;
                    while (rs_is_digit(text[i])) {
                        i++;
                    }
                } else {
                    i = save;               /* 不是指数，回退 */
                }
            }
            t->type = RS_TOK_NUMBER;
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
                    } else if (rs_is_digit(d)) {
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
                        while (k < t->len && rs_is_digit(t->p[k])) {
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
        if (rs_is_alpha(c)) {
            uint16_t start = i;
            while (rs_is_alpha(text[i]) || rs_is_digit(text[i])) {
                i++;
            }
            t->type = RS_TOK_WORD;
            t->p = &text[start];
            t->len = (uint16_t)(i - start);
            n++;
            continue;
        }
        /* 运算符：双字符优先（含 ++ --）；逗号作为独立 token（for/call 分隔） */
        t->type = RS_TOK_OP;
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
    toks[n].type = RS_TOK_EOF;
    return (int)(n + 1U);
}

/* 取字符串 token 的去转义内容；未闭合返回 false */
bool rs_str_unescape(const rs_tok_t *t, char *dst, uint16_t cap, uint16_t *out_len)
{
    uint16_t i = 1U;
    uint16_t o = 0U;
    if (t->type != RS_TOK_STRING || t->len < 2U) {
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

static const char *const rs_kws[] = {
    "if", "elseif", "else", "end", "for", "while",
    "function", "break", "return"
};

/* 只识别行首关键字（大小写敏感），用于扫描与 if 链深度扫描 */
int rs_line_kind(const char *line)
{
    char buf[RS_NAME_MAX];
    uint16_t i = 0U;
    uint16_t n = 0U;
    uint32_t k;
    while (rs_is_space(line[i])) {
        i++;
    }
    if (!rs_is_alpha(line[i])) {
        return K_NONE;
    }
    while (rs_is_alpha(line[i]) || rs_is_digit(line[i])) {
        if (n + 1U < sizeof(buf)) {
            buf[n++] = line[i];
        } else {
            return K_NONE;
        }
        i++;
    }
    buf[n] = '\0';
    for (k = 0U; k < sizeof(rs_kws) / sizeof(rs_kws[0]); k++) {
        if (strcmp(buf, rs_kws[k]) == 0) {
            return (int)(K_IF + k);
        }
    }
    return K_NONE;
}
