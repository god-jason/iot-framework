/*
 * args_test.c - 命令参数解析（components/cmd/args）单元测试
 *
 * 覆盖：零参数命令、位置参数/必填、选项与开关、默认值取值、
 *       查找接口、错误码与文本、非法入参边界。
 */

#include "test.h"
#include "args.h"

#include <string.h>

/* 常规参数表：位置参数 + 长/短选项 + 布尔开关 */
static const args_def_t s_args[] = {
    { "host",    0,   ARGS_TYPE_STR,  true,  true,  NULL,  "target host" },
    { "count",   'c', ARGS_TYPE_UINT, false, false, "4",   "count" },
    { "delay",   'd', ARGS_TYPE_INT,  false, false, "-1",  "delay" },
    { "verbose", 'v', ARGS_TYPE_BOOL, false, false, NULL,  "verbose" }
};
#define S_ARG_COUNT ((uint8_t)(sizeof(s_args) / sizeof(s_args[0])))

/* 各类型默认值（x 的默认值无法解析为整数，用于验证失败返回） */
static const args_def_t s_def_args[] = {
    { "s", 0, ARGS_TYPE_STR,  true, false, "hello", NULL },
    { "i", 0, ARGS_TYPE_INT,  true, false, "-7",    NULL },
    { "u", 0, ARGS_TYPE_UINT, true, false, "9",     NULL },
    { "b", 0, ARGS_TYPE_BOOL, true, false, "on",    NULL },
    { "x", 0, ARGS_TYPE_INT,  true, false, "oops",  NULL }
};
#define S_DEF_COUNT ((uint8_t)(sizeof(s_def_args) / sizeof(s_def_args[0])))

/* required=true 且带默认值：默认值不能替代必填 */
static const args_def_t s_req_args[] = {
    { "req", 0, ARGS_TYPE_STR, true, true, "dv", NULL }
};

void ut_args(void)
{
    args_parsed_t p;
    const char *s = NULL;
    const char *so = NULL;
    unsigned u = 0U;
    int i = 0;
    bool b = false;

    /* ---------------- 零参数命令（quit/exit 回归） ---------------- */
    tf_begin("args/零参数命令");
    {
        char *extra[] = { "x" };

        TF_CHECK_INT(args_parse(NULL, 0U, NULL, 0, &p), ARGS_OK);
        TF_CHECK(p.args == NULL);
        TF_CHECK_INT(p.arg_count, 0);
        TF_CHECK_INT(p.positionals, 0);
        TF_CHECK_INT(p.error, ARGS_OK);

        /* 零参数命令仍拒绝多余位置参数 */
        TF_CHECK_INT(args_parse(NULL, 0U, extra, 1, &p), ARGS_ERR_EXTRA);
        TF_CHECK_INT(p.error, ARGS_ERR_EXTRA);

        /* args 为 NULL 但声明了参数个数：非法 */
        TF_CHECK_INT(args_parse(NULL, 1U, NULL, 0, &p), ARGS_ERR_PARAM);
    }
    tf_end();

    /* ---------------- 位置参数与必填 ---------------- */
    tf_begin("args/位置参数与必填");
    {
        char *one[] = { "box" };
        char *two[] = { "box", "extra" };
        char *dash[] = { "-" };
        char *has_null[] = { "box", NULL };

        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, one, 1, &p), ARGS_OK);
        TF_CHECK(p.args == s_args);
        TF_CHECK_INT(p.arg_count, S_ARG_COUNT);
        TF_CHECK(args_get_str(&p, 0U, &s) && strcmp(s, "box") == 0);
        TF_CHECK(args_get_uint(&p, 1U, &u) && u == 4U);
        TF_CHECK(args_get_int(&p, 2U, &i) && i == -1);
        TF_CHECK(args_get_bool(&p, 3U, &b) && !b);

        /* 必填缺失 */
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, NULL, 0, &p),
                     ARGS_ERR_MISSING_ARG);
        TF_CHECK_INT(p.error, ARGS_ERR_MISSING_ARG);
        TF_CHECK(strcmp(p.err_arg, "host") == 0);

        /* 多余位置参数 */
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, two, 2, &p),
                     ARGS_ERR_EXTRA);

        /* 单独 "-" 不是选项，按位置参数处理 */
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, dash, 1, &p), ARGS_OK);
        TF_CHECK(args_get_str(&p, 0U, &s) && strcmp(s, "-") == 0);

        /* argv 中间出现 NULL */
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, has_null, 2, &p),
                     ARGS_ERR_PARAM);
    }
    tf_end();

    /* ---------------- 选项与开关 ---------------- */
    tf_begin("args/选项与开关");
    {
        char *longv[] = { "h", "--count", "10", "--delay=5", "--verbose" };
        char *shortv[] = { "h", "-c", "3", "-d", "-2", "-v" };
        char *bool_on[] = { "h", "--verbose=on" };
        char *bool_off[] = { "h", "--verbose=0" };
        char *unknown[] = { "h", "--bad", "1" };
        char *unknown_short[] = { "h", "-z" };
        char *noval[] = { "h", "--count" };
        char *badval[] = { "h", "--count", "abc" };
        char *badbool[] = { "h", "--verbose=maybe" };
        char *term[] = { "--", "--count" };

        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, longv, 5, &p), ARGS_OK);
        TF_CHECK(args_get_uint(&p, 1U, &u) && u == 10U);
        TF_CHECK(args_get_int(&p, 2U, &i) && i == 5);
        TF_CHECK(args_get_bool(&p, 3U, &b) && b);

        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, shortv, 6, &p), ARGS_OK);
        TF_CHECK(args_get_uint(&p, 1U, &u) && u == 3U);
        TF_CHECK(args_get_int(&p, 2U, &i) && i == -2);
        TF_CHECK(args_get_bool(&p, 3U, &b) && b);

        /* 布尔开关带值 */
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, bool_on, 2, &p), ARGS_OK);
        TF_CHECK(args_get_bool(&p, 3U, &b) && b);
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, bool_off, 2, &p), ARGS_OK);
        TF_CHECK(args_get_bool(&p, 3U, &b) && !b);

        /* 错误路径 */
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, unknown, 3, &p),
                     ARGS_ERR_UNKNOWN);
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, unknown_short, 2, &p),
                     ARGS_ERR_UNKNOWN);
        TF_CHECK(strcmp(p.err_arg, "-z") == 0);
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, noval, 2, &p),
                     ARGS_ERR_MISSING_VALUE);
        TF_CHECK(strcmp(p.err_arg, "count") == 0);
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, badval, 3, &p),
                     ARGS_ERR_TYPE);
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, badbool, 2, &p),
                     ARGS_ERR_TYPE);

        /* "--" 之后全部按位置参数 */
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, term, 2, &p), ARGS_OK);
        TF_CHECK(args_get_str(&p, 0U, &s) && strcmp(s, "--count") == 0);
        TF_CHECK(args_get_uint(&p, 1U, &u) && u == 4U);
    }
    tf_end();

    /* ---------------- 取值接口与默认值 ---------------- */
    tf_begin("args/取值接口与默认值");
    {
        TF_CHECK_INT(args_parse(s_def_args, S_DEF_COUNT, NULL, 0, &p), ARGS_OK);
        TF_CHECK(args_get_str(&p, 0U, &s) && strcmp(s, "hello") == 0);
        TF_CHECK(args_get_int(&p, 1U, &i) && i == -7);
        TF_CHECK(args_get_uint(&p, 2U, &u) && u == 9U);
        TF_CHECK(args_get_bool(&p, 3U, &b) && b);

        /* 默认值类型不匹配 / 类型交叉读取 */
        TF_CHECK(!args_get_int(&p, 4U, &i));
        TF_CHECK(!args_get_uint(&p, 0U, &u));
        TF_CHECK(!args_get_int(&p, 0U, &i));

        /* 下标越界、输出指针为空、parsed 为空 */
        TF_CHECK(!args_get_str(&p, 20U, &s));
        TF_CHECK(!args_get_int(&p, 1U, NULL));
        TF_CHECK(!args_get_str(&p, 0U, NULL));
        TF_CHECK(!args_get_uint(&p, 2U, NULL));
        TF_CHECK(!args_get_bool(&p, 3U, NULL));
        TF_CHECK(!args_get_str(NULL, 0U, &so));
        TF_CHECK(!args_get_int(NULL, 0U, &i));
        TF_CHECK(!args_get_uint(NULL, 0U, &u));
        TF_CHECK(!args_get_bool(NULL, 0U, &b));

        /* required=true + 默认值：默认值不能满足必填 */
        TF_CHECK_INT(args_parse(s_req_args, 1U, NULL, 0, &p),
                     ARGS_ERR_MISSING_ARG);
        TF_CHECK(strcmp(p.err_arg, "req") == 0);
        {
            char *v[] = { "val" };

            TF_CHECK_INT(args_parse(s_req_args, 1U, v, 1, &p), ARGS_OK);
            TF_CHECK(args_get_str(&p, 0U, &s) && strcmp(s, "val") == 0);
        }
    }
    tf_end();

    /* ---------------- 查找与错误文本 ---------------- */
    tf_begin("args/查找与错误文本");
    {
        static const struct {
            int code;
            const char *text;
        } errors[] = {
            { ARGS_OK,                "ok" },
            { ARGS_ERR_PARAM,         "invalid parameter" },
            { ARGS_ERR_UNKNOWN,       "unknown option" },
            { ARGS_ERR_MISSING_VALUE, "missing option value" },
            { ARGS_ERR_MISSING_ARG,   "missing required argument" },
            { ARGS_ERR_TYPE,          "invalid value type" },
            { ARGS_ERR_EXTRA,         "unexpected argument" }
        };
        uint32_t k;

        TF_CHECK_INT(args_find(s_args, S_ARG_COUNT, "count"), 1);
        TF_CHECK_INT(args_find(s_args, S_ARG_COUNT, "verbose"), 3);
        TF_CHECK_INT(args_find(s_args, S_ARG_COUNT, "nope"), -1);
        TF_CHECK_INT(args_find(s_args, S_ARG_COUNT, NULL), -1);
        TF_CHECK_INT(args_find(NULL, 0U, "count"), -1);

        TF_CHECK_INT(args_find_short(s_args, S_ARG_COUNT, 'd'), 2);
        TF_CHECK_INT(args_find_short(s_args, S_ARG_COUNT, 'z'), -1);
        TF_CHECK_INT(args_find_short(s_args, S_ARG_COUNT, '\0'), -1);
        TF_CHECK_INT(args_find_short(NULL, 0U, 'v'), -1);

        for (k = 0U; k < sizeof(errors) / sizeof(errors[0]); k++) {
            TF_CHECK(strcmp(args_error_str(errors[k].code),
                            errors[k].text) == 0);
        }
        TF_CHECK(strcmp(args_error_str(999), "command error") == 0);
    }
    tf_end();

    /* ---------------- 非法入参边界 ---------------- */
    tf_begin("args/非法入参");
    {
        char *one[] = { "box" };

        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, NULL, 0, NULL),
                     ARGS_ERR_PARAM);
        TF_CHECK_INT(args_parse(s_args, (uint8_t)(ARGS_MAX + 1U),
                                NULL, 0, &p), ARGS_ERR_PARAM);
        TF_CHECK_INT(args_parse(NULL, 0U, NULL, 0, NULL), ARGS_ERR_PARAM);
        TF_CHECK_INT(args_parse(s_args, S_ARG_COUNT, one, 0, &p),
                     ARGS_ERR_MISSING_ARG);
    }
    tf_end();
}
