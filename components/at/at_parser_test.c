/* components/modem/ml307/at_parser.c per-file 单元测试。
 *
 * 覆盖：初始化参数校验、单行/多行/分片输入、空行、CR/LF 处理、
 *       ">" 提示符、行中 '>'、缓冲溢出与溢出后恢复、reset。
 * 未覆盖：AT_BINARY 类型（实现中未产生）。
 */
#include "test.h"
#include "at_parser.h"

#include <string.h>

#define TOK_MAX 16
#define TOK_TEXT_MAX 128

typedef struct {
    at_token_type_e type;
    char text[TOK_TEXT_MAX];
    uint32_t len;
} tok_rec_t;

static tok_rec_t s_toks[TOK_MAX];
static uint32_t s_tok_count;
static uint8_t s_line_buf[64];
static uint8_t s_small_buf[8];
static uint8_t s_one_buf[1];

static void on_token(const at_token_t *token, void *user)
{
    tok_rec_t *rec;

    (void)user;
    if (s_tok_count >= TOK_MAX) {
        return;
    }
    rec = &s_toks[s_tok_count++];
    rec->type = token->type;
    rec->len = token->len;
    memset(rec->text, 0, sizeof(rec->text));
    if (token->data != NULL) {
        uint32_t n = token->len;
        if (n > sizeof(rec->text) - 1) {
            n = sizeof(rec->text) - 1;
        }
        memcpy(rec->text, token->data, n);
    }
}

static void reset_tokens(void)
{
    s_tok_count = 0;
    memset(s_toks, 0, sizeof(s_toks));
}

static void feed(const char *s)
{
    at_parser_input((const uint8_t *)s, (uint32_t)strlen(s));
}

static void feed_n(const char *s, uint32_t n)
{
    at_parser_input((const uint8_t *)s, n);
}

static void init_default(void)
{
    reset_tokens();
    at_parser_init(s_line_buf, sizeof(s_line_buf), on_token, NULL);
}

static void test_init(void)
{
    tf_begin("at_parser/初始化参数校验");
    TF_CHECK_INT(at_parser_init(NULL, 16, on_token, NULL), -1);
    TF_CHECK_INT(at_parser_init(s_line_buf, 0, on_token, NULL), -1);
    TF_CHECK_INT(at_parser_init(s_line_buf, sizeof(s_line_buf), NULL, NULL), -1);
    TF_CHECK_INT(at_parser_init(s_line_buf, sizeof(s_line_buf), on_token, NULL),
                 0);
    tf_end();

    tf_begin("at_parser/空输入不产生 token");
    reset_tokens();
    TF_CHECK_INT(at_parser_input(NULL, 5), 0);
    TF_CHECK_INT(at_parser_input((const uint8_t *)"abc", 0), 0);
    TF_CHECK_INT(s_tok_count, 0);
    tf_end();
}

static void test_lines(void)
{
    init_default();

    tf_begin("at_parser/单行 CRLF");
    feed("OK\r\n");
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_INT(s_toks[0].type, AT_LINE);
    TF_CHECK_INT(s_toks[0].len, 2);
    TF_CHECK_STR(s_toks[0].text, "OK");
    tf_end();

    tf_begin("at_parser/单行 LF");
    init_default();
    feed("READY\n");
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_STR(s_toks[0].text, "READY");
    tf_end();

    tf_begin("at_parser/空行不产生 token");
    init_default();
    feed("\r\n");
    feed("\n");
    TF_CHECK_INT(s_tok_count, 0);
    tf_end();

    tf_begin("at_parser/一次输入多行");
    init_default();
    feed("A\r\nB\r\nC\r\n");
    TF_CHECK_INT(s_tok_count, 3);
    TF_CHECK_STR(s_toks[0].text, "A");
    TF_CHECK_STR(s_toks[1].text, "B");
    TF_CHECK_STR(s_toks[2].text, "C");
    tf_end();

    tf_begin("at_parser/分片拼接");
    init_default();
    feed("AB");
    TF_CHECK_INT(s_tok_count, 0);
    feed("C\r");
    TF_CHECK_INT(s_tok_count, 0);
    feed("\n");
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_STR(s_toks[0].text, "ABC");
    TF_CHECK_INT(s_toks[0].len, 3);
    tf_end();

    tf_begin("at_parser/内嵌 CR 被忽略");
    init_default();
    feed_n("A\rB\r\n", 5);
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_STR(s_toks[0].text, "AB");
    tf_end();

    tf_begin("at_parser/reset 清空半行");
    init_default();
    feed("AB");
    TF_CHECK_INT(at_parser_reset(), 0);
    feed("CD\n");
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_STR(s_toks[0].text, "CD");
    tf_end();
}

static void test_prompt(void)
{
    init_default();

    tf_begin("at_parser/行首 > 提示符");
    feed(">");
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_INT(s_toks[0].type, AT_PROMPT);
    TF_CHECK_INT(s_toks[0].len, 0);
    tf_end();

    tf_begin("at_parser/> 后跟 CRLF 只产生一个提示符");
    init_default();
    feed(">\r\n");
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_INT(s_toks[0].type, AT_PROMPT);
    tf_end();

    tf_begin("at_parser/行中 > 作为普通字符");
    init_default();
    feed("AB>CD\r\n");
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_INT(s_toks[0].type, AT_LINE);
    TF_CHECK_STR(s_toks[0].text, "AB>CD");
    TF_CHECK_INT(s_toks[0].len, 5);
    tf_end();

    tf_begin("at_parser/缓冲恰好满时 > 触发溢出");
    reset_tokens();
    at_parser_init(s_small_buf, sizeof(s_small_buf), on_token, NULL);
    feed("1234567");
    TF_CHECK_INT(s_tok_count, 0);
    feed(">");
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_INT(s_toks[0].type, AT_OVERFLOW);
    tf_end();
}

static void test_overflow(void)
{
    tf_begin("at_parser/超长行触发溢出");
    reset_tokens();
    at_parser_init(s_small_buf, sizeof(s_small_buf), on_token, NULL);
    feed("1234567");              /* 恰好填满 7 字节（size-1） */
    TF_CHECK_INT(s_tok_count, 0);
    feed("8");                    /* 第 8 字节溢出 */
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_INT(s_toks[0].type, AT_OVERFLOW);
    feed("\r\n");
    TF_CHECK_INT(s_tok_count, 1); /* 溢出后已复位，空行不产生 token */
    tf_end();

    tf_begin("at_parser/溢出后余下字符组成新行");
    reset_tokens();
    at_parser_init(s_small_buf, sizeof(s_small_buf), on_token, NULL);
    feed("123456789\r\n");
    TF_CHECK_INT(s_tok_count, 2);
    TF_CHECK_INT(s_toks[0].type, AT_OVERFLOW);
    TF_CHECK_INT(s_toks[1].type, AT_LINE);
    TF_CHECK_STR(s_toks[1].text, "9");
    tf_end();

    tf_begin("at_parser/容量为 1 时任何字符都溢出");
    reset_tokens();
    at_parser_init(s_one_buf, sizeof(s_one_buf), on_token, NULL);
    feed("A");
    TF_CHECK_INT(s_tok_count, 1);
    TF_CHECK_INT(s_toks[0].type, AT_OVERFLOW);
    tf_end();
}

void ut_at_parser(void)
{
    test_init();
    test_lines();
    test_prompt();
    test_overflow();
}
