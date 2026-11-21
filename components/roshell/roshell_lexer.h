#ifndef RS_LEXER_H
#define RS_LEXER_H

/*
 * roshell_lexer.h - roshell 词法模块内部声明
 *
 * token 类型与结构、字符分类、关键字 / 运算符匹配、单行词法分析、
 * 字符串去转义与行首关键字分类。实现见 roshell_lexer.c。
 * 仅供 roshell 各实现文件使用，平台 / 组件代码不应依赖。
 */

#include "roshell.h"

/* ------------------------------ token 类型 ------------------------------ */

typedef enum {
    RS_TOK_EOF = 0,
    RS_TOK_WORD,
    RS_TOK_STRING,
    RS_TOK_NUMBER,
    RS_TOK_VAR,
    RS_TOK_OP
} rs_tok_type_t;

typedef struct {
    uint8_t     type;
    const char *p;
    uint16_t    len;
    int32_t     ival;
    float       fval;
    bool        is_float;
} rs_tok_t;

/* 字符分类小工具 */
bool rs_is_alpha(char c);
bool rs_is_digit(char c);
bool rs_is_space(char c);

/* 关键字/单词精确匹配（大小写敏感，规格 §2） */
bool rs_kw(const rs_tok_t *t, const char *s);

/* 运算符 token 匹配 */
bool rs_op(const rs_tok_t *t, const char *s);

/* 单行词法分析：清空并逐个追加到 toks（末尾追加 RS_TOK_EOF），返回 token 数 */
int rs_lex(const char *text, vec_t *toks);

/* 取字符串 token 的去转义内容；未闭合返回 false */
bool rs_str_unescape(const rs_tok_t *t, char *dst, uint16_t cap, uint16_t *out_len);

/* 行首关键字分类 */
enum {
    K_NONE = 0, K_IF, K_ELSEIF, K_ELSE, K_END, K_FOR, K_WHILE,
    K_FUNCTION, K_BREAK, K_RETURN
};

/* 只识别行首关键字（大小写敏感），用于扫描与 if 链深度扫描 */
int rs_line_kind(const char *line);

#endif /* RS_LEXER_H */
