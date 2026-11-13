/*
 * cmd_test.c - 命令定义与参数解析回归测试
 */

#include "test.h"
#include "cmd.h"

#include <string.h>

static const args_def_t s_ping_args[] = {
    { "host", 0, ARGS_TYPE_STR, true, true, NULL, "target host" },
    { "count", 'c', ARGS_TYPE_UINT, false, false, "4", "packet count" },
    { "timeout", 't', ARGS_TYPE_UINT, false, false, "1000", "timeout ms" },
    { "verbose", 'v', ARGS_TYPE_BOOL, false, false, NULL, "verbose output" }
};

static const cmd_def_t s_ping_def = {
    "ping", "send ICMP echo", s_ping_args, 4U
};

static int s_run_value;
static int s_run_called;

static int ping_handler(const args_parsed_t *parsed, void *user)
{
    unsigned count = 0U;

    (void)user;
    s_run_called++;
    if (args_get_uint(parsed, 1U, &count)) {
        s_run_value = (int)count;
    }
    return 42;
}

static const cmd_entry_t s_table[] = {
    { &s_ping_def, ping_handler }
};

#define PARSE(argv_, argc_, out_) \
    args_parse(s_ping_def.args, s_ping_def.arg_count, (argv_), (argc_), (out_))

void ut_cmd(void)
{
    args_parsed_t p;
    const char *s = NULL;
    unsigned u = 0U;
    bool b = false;
    char buf[512];

    tf_begin("cmd/定义与参数解析");

    /* 位置参数 + 默认值 */
    {
        char *argv[] = { "example.com" };

        TF_CHECK_INT(PARSE(argv, 1, &p), ARGS_OK);
        TF_CHECK(args_get_str(&p, 0U, &s) && strcmp(s, "example.com") == 0);
        TF_CHECK(args_get_uint(&p, 1U, &u) && u == 4U);
        TF_CHECK(args_get_bool(&p, 3U, &b) && !b);
    }

    /* 长选项 + 短选项 + 开关 */
    {
        char *argv[] = { "host", "--count", "10", "-v" };

        TF_CHECK_INT(PARSE(argv, 4, &p), ARGS_OK);
        TF_CHECK(args_get_uint(&p, 1U, &u) && u == 10U);
        TF_CHECK(args_get_bool(&p, 3U, &b) && b);
        TF_CHECK(!args_get_bool(&p, 1U, &b));    /* UINT 值不能按布尔解析 */
    }

    /* --name=value 形式 */
    {
        char *argv[] = { "host", "--count=7", "--timeout=500" };

        TF_CHECK_INT(PARSE(argv, 3, &p), ARGS_OK);
        TF_CHECK(args_get_uint(&p, 1U, &u) && u == 7U);
        TF_CHECK(args_get_uint(&p, 2U, &u) && u == 500U);
    }

    /* 短选项取值 */
    {
        char *argv[] = { "host", "-c", "3" };

        TF_CHECK_INT(PARSE(argv, 3, &p), ARGS_OK);
        TF_CHECK(args_get_uint(&p, 1U, &u) && u == 3U);
    }

    /* -- 终止符：后续按位置参数 */
    {
        char *argv[] = { "--", "--count" };

        TF_CHECK_INT(PARSE(argv, 2, &p), ARGS_OK);
        TF_CHECK(args_get_str(&p, 0U, &s) && strcmp(s, "--count") == 0);
        TF_CHECK(args_get_uint(&p, 1U, &u) && u == 4U);
    }

    /* 错误路径 */
    {
        char *empty[] = { 0 };
        char *unknown[] = { "host", "--bad", "1" };
        char *noval[] = { "host", "--count" };
        char *badval[] = { "host", "--count", "abc" };
        char *badbool[] = { "host", "--verbose=maybe" };
        char *extra[] = { "host", "extra" };

        TF_CHECK_INT(PARSE(empty, 0, &p), ARGS_ERR_MISSING_ARG);
        TF_CHECK_INT(PARSE(unknown, 3, &p), ARGS_ERR_UNKNOWN);
        TF_CHECK_INT(PARSE(noval, 2, &p), ARGS_ERR_MISSING_VALUE);
        TF_CHECK_INT(PARSE(badval, 3, &p), ARGS_ERR_TYPE);
        TF_CHECK_INT(PARSE(badbool, 2, &p), ARGS_ERR_TYPE);
        TF_CHECK_INT(PARSE(extra, 2, &p), ARGS_ERR_EXTRA);
        TF_CHECK_INT(args_parse(NULL, 0U, NULL, 0, NULL), ARGS_ERR_PARAM);
    }

    /* 查找与错误文本 */
    TF_CHECK_INT(args_find(s_ping_def.args, s_ping_def.arg_count, "count"), 1);
    TF_CHECK_INT(args_find_short(s_ping_def.args, s_ping_def.arg_count, 'v'), 3);
    TF_CHECK_INT(args_find(s_ping_def.args, s_ping_def.arg_count, "nope"), -1);
    TF_CHECK(strcmp(args_error_str(ARGS_ERR_TYPE), "invalid value type") == 0);

    /* usage / help 文本 */
    TF_CHECK(cmd_usage(&s_ping_def, buf, sizeof(buf)) > 0);
    TF_CHECK(strstr(buf, "ping <host>") != NULL);
    TF_CHECK(strstr(buf, "[--count <n>]") != NULL);
    TF_CHECK(strstr(buf, "[--verbose]") != NULL);
    TF_CHECK(cmd_help(&s_ping_def, buf, sizeof(buf)) > 0);
    TF_CHECK(strstr(buf, "send ICMP echo") != NULL);
    TF_CHECK(strstr(buf, "default: 4") != NULL);

    /* 命令表分发 */
    {
        char *argv[] = { "ping", "host", "-c", "9" };
        char *miss[] = { "nope" };

        s_run_called = 0;
        s_run_value = 0;
        TF_CHECK_INT(cmd_run(s_table, 1U, 4, argv, NULL), 42);
        TF_CHECK_INT(s_run_called, 1);
        TF_CHECK_INT(s_run_value, 9);
        TF_CHECK_INT(cmd_run(s_table, 1U, 1, miss, NULL), ARGS_ERR_UNKNOWN);
        TF_CHECK_INT(cmd_run(s_table, 1U, 0, NULL, NULL), ARGS_ERR_PARAM);
    }

    tf_end();
}
