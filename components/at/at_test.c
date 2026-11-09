/*
 * at_test.c - AT 引擎组件测试（at / at_parser / at_urc）
 *
 * 覆盖：解析器分行与提示符、溢出、URC 注册与分发、
 *       引擎初始化/启动/停止、输入注入与状态机、参数校验。
 */

#include "test.h"
#include "at.h"
#include "at_parser.h"
#include "at_urc.h"

/* ============================== parser ============================== */

static char s_lines[4][32];
static int s_line_count;
static int s_prompt_count;
static int s_overflow_count;

static void token_cb(const at_token_t *token, void *user)
{
    (void)user;

    if (token->type == AT_LINE) {
        if (s_line_count < 4) {
            uint32_t n = token->len;

            if (n > sizeof(s_lines[0]) - 1U) {
                n = sizeof(s_lines[0]) - 1U;
            }
            memcpy(s_lines[s_line_count], token->data, n);
            s_lines[s_line_count][n] = '\0';
            s_line_count++;
        }
    } else if (token->type == AT_PROMPT) {
        s_prompt_count++;
    } else if (token->type == AT_OVERFLOW) {
        s_overflow_count++;
    }
}

void ut_at_parser(void)
{
    char buf[16];

    tf_begin("at_parser/分行与提示符");

    s_line_count = 0;
    s_prompt_count = 0;
    s_overflow_count = 0;

    TF_CHECK_INT(at_parser_init(NULL, sizeof(buf), token_cb, NULL), -1);
    TF_CHECK_INT(at_parser_init(buf, 0U, token_cb, NULL), -1);
    TF_CHECK_INT(at_parser_init(buf, sizeof(buf), NULL, NULL), -1);
    TF_CHECK_INT(at_parser_init(buf, sizeof(buf), token_cb, NULL), 0);

    TF_CHECK_INT(at_parser_input((const uint8_t *)"OK\r\n", 4U), 0);
    TF_CHECK_INT(s_line_count, 1);
    TF_CHECK_STR(s_lines[0], "OK");

    /* 一次到达多行 */
    {
        static const char multi[] = "+CSQ: 20\r\nERROR\r\n";

        TF_CHECK_INT(at_parser_input((const uint8_t *)multi,
                                     (uint32_t)strlen(multi)), 0);
    }
    TF_CHECK_INT(s_line_count, 3);
    TF_CHECK_STR(s_lines[1], "+CSQ: 20");
    TF_CHECK_STR(s_lines[2], "ERROR");

    /* 行首 '>' 为数据提示符 */
    TF_CHECK_INT(at_parser_input((const uint8_t *)">", 1U), 0);
    TF_CHECK_INT(s_prompt_count, 1);

    /* 行中间的 '>' 不构成提示符 */
    TF_CHECK_INT(at_parser_input((const uint8_t *)"a>b\r\n", 5U), 0);
    TF_CHECK_INT(s_prompt_count, 1);
    TF_CHECK_STR(s_lines[3], "a>b");

    tf_end();

    tf_begin("at_parser/溢出与复位");

    s_line_count = 0;
    s_overflow_count = 0;
    TF_CHECK_INT(at_parser_reset(), 0);

    {
        /* 超过 15 字符的行触发溢出；剩余字符构成新的一行 */
        static const char longline[] = "0123456789ABCDEFGHIJ\r\n";
        TF_CHECK_INT(at_parser_input((const uint8_t *)longline,
                                     (uint32_t)strlen(longline)), 0);
        TF_CHECK_INT(s_overflow_count, 1);
        TF_CHECK_INT(s_line_count, 1);
    }

    TF_CHECK_INT(at_parser_input(NULL, 4U), 0);
    TF_CHECK_INT(at_parser_input((const uint8_t *)"x", 0U), 0);
    TF_CHECK_INT(at_parser_reset(), 0);

    tf_end();
}

/* ============================== urc ============================== */

static int s_urc_count;
static char s_urc_line[64];

static void urc_cb(const char *line, uint32_t len, void *user)
{
    uint32_t n = len;

    (void)user;
    if (n > sizeof(s_urc_line) - 1U) {
        n = sizeof(s_urc_line) - 1U;
    }
    memcpy(s_urc_line, line, n);
    s_urc_line[n] = '\0';
    s_urc_count++;
}

void ut_at_urc(void)
{
    tf_begin("at_urc/注册与分发");

    TF_CHECK_INT(at_urc_register(NULL, urc_cb, NULL), -1);
    TF_CHECK_INT(at_urc_register("+CSQ", NULL, NULL), -1);
    TF_CHECK_INT(at_urc_register("+CSQ", urc_cb, NULL), 0);

    s_urc_count = 0;
    TF_CHECK_INT(at_urc_dispatch("+CSQ: 25,99", 11U), 0);
    TF_CHECK_INT(s_urc_count, 1);
    TF_CHECK_STR(s_urc_line, "+CSQ: 25,99");

    /* 非行首匹配不触发 */
    TF_CHECK_INT(at_urc_dispatch("OK +CSQ", 7U), -1);
    TF_CHECK_INT(s_urc_count, 1);

    /* 未注册前缀 */
    TF_CHECK_INT(at_urc_dispatch("+CREG: 1", 8U), -1);

    TF_CHECK_INT(at_urc_unregister("+CSQ"), 0);
    TF_CHECK_INT(at_urc_unregister("+CSQ"), -1);   /* 已注销 */
    TF_CHECK_INT(at_urc_unregister(NULL), -1);

    tf_end();
}

/* ============================== engine ============================== */

void ut_at(void)
{
    at_config_t cfg;
    uint8_t rx[32];
    char line[64];

    tf_begin("at/初始化与启停");

    TF_CHECK_INT(at_init(NULL), -1);

    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = NULL;                     /* 必须非 NULL */
    cfg.rx_buffer = rx;
    cfg.rx_buffer_size = sizeof(rx);
    cfg.line_buffer = line;
    cfg.line_buffer_size = sizeof(line);
    cfg.default_timeout = 10U;
    TF_CHECK_INT(at_init(&cfg), -1);

    /* 用一个非空句柄占位（PC 模拟端不实际收发） */
    cfg.uart = (hal_uart_t *)rx;
    TF_CHECK_INT(at_init(&cfg), 0);

    TF_CHECK_INT(at_start(), 0);
    TF_CHECK_INT(at_get_state(), AT_IDLE);
    TF_CHECK(!at_is_busy());

    TF_CHECK_INT(at_deinit(), 0);
    TF_CHECK_INT(at_stop(), 0);

    tf_end();

    tf_begin("at/输入注入与状态机");

    TF_CHECK_INT(at_init(&cfg), 0);
    TF_CHECK_INT(at_start(), 0);

    at_input((const uint8_t *)"OK\r\n", 4U);
    TF_CHECK_INT(at_process(), 0);

    /* 无在途事务：取消失败 */
    TF_CHECK_INT(at_cancel(), -1);

    TF_CHECK_INT(at_send(NULL), -1);
    TF_CHECK_INT(at_send_raw(NULL, 1U), -1);
    TF_CHECK_INT(at_send_raw((const uint8_t *)"AT", 0U), -1);

    tf_end();

    tf_begin("at/阻塞执行（超时路径）");

    TF_CHECK_INT(at_init(&cfg), 0);
    TF_CHECK_INT(at_start(), 0);

    /* 不注入任何响应，1 次 process 超时 */
    {
        at_result_e r = at_exec("AT", 1U);
        TF_CHECK(r == AT_RESULT_TIMEOUT || r == AT_RESULT_ERROR);
    }

    TF_CHECK(at_get_response() != NULL);

    tf_end();
}
