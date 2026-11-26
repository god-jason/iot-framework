/*
 * roshell_cmd_table_test.c - 命令表定义与参数解析回归测试
 */

#include "test.h"
#include "roshell.h"
#include "ros.h"

#include <string.h>

static const rs_args_t s_ping_args[] = {
    { "host", 0, RS_ARGS_TYPE_STR, true, true, NULL, "target host" },
    { "count", 'c', RS_ARGS_TYPE_UINT, false, false, "4", "packet count" },
    { "timeout", 't', RS_ARGS_TYPE_UINT, false, false, "1000", "timeout ms" },
    { "verbose", 'v', RS_ARGS_TYPE_BOOL, false, false, NULL, "verbose output" }
};

static const rs_cmd_t s_ping_def = {
    "ping", "send ICMP echo", NULL, NULL, s_ping_args, 4U,
    NULL, NULL, NULL, NULL, NULL, NULL
};

static int s_run_value;
static int s_run_called;

static int ping_handler(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    unsigned count = 0U;

    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;
    s_run_called++;
    if (rs_args_get_uint(parsed, 1U, &count)) {
        s_run_value = (int)count;
    }
    return 42;
}

static const rs_cmd_t s_table[] = {
    { "ping", "send ICMP echo", NULL, NULL, s_ping_args, 4U, ping_handler,
      NULL, NULL, NULL, NULL, NULL }
};

/* ---------------- 生命周期钩子 ---------------- */

static int s_hook_pause_calls;
static int s_hook_resume_calls;
static int s_hook_kill_calls;

static int hook_pause(roshell_t *rs, void *user)
{
    (void)user;
    (void)rs;
    s_hook_pause_calls++;
    return 0;
}

static int hook_resume(roshell_t *rs, void *user)
{
    (void)user;
    (void)rs;
    s_hook_resume_calls++;
    return 0;
}

static int hook_kill(roshell_t *rs, void *user)
{
    (void)user;
    (void)rs;
    s_hook_kill_calls++;
    return 0;
}

/* 在命令执行期间模拟 pause/resume/kill 通知 */
static int hook_handler(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)parsed;
    (void)user;

    TF_CHECK_INT(rs_cmd_notify_pause(ros_current_task), 0);
    TF_CHECK_INT(rs_cmd_notify_resume(ros_current_task), 0);
    TF_CHECK_INT(rs_cmd_notify_kill(ros_current_task), 0);
    /* kill 已清除登记：再次通知不应触发钩子 */
    TF_CHECK_INT(rs_cmd_notify_pause(ros_current_task), 0);
    return 0;
}

static int deny_pause(roshell_t *rs, void *user)
{
    (void)user;
    (void)rs;
    return -7;
}

static int deny_handler(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)parsed;
    (void)user;
    TF_CHECK_INT(rs_cmd_notify_pause(ros_current_task), -7);
    return 0;
}

static const rs_cmd_t s_hook_def = {
    "hookcmd", "hook test", NULL, NULL, NULL, 0U,
    hook_handler, NULL, hook_pause, hook_resume, hook_kill, NULL
};
static const rs_cmd_t s_deny_def = {
    "denycmd", "deny pause", NULL, NULL, NULL, 0U,
    deny_handler, NULL, deny_pause, NULL, NULL, NULL
};

#define PARSE(argv_, argc_, out_) \
    rs_args_parse(s_ping_def.args, s_ping_def.arg_count, (argv_), (argc_), (out_))

void ut_cmd(void)
{
    rs_args_parsed_t p;
    const char *s = NULL;
    unsigned u = 0U;
    bool b = false;
    char buf[512];

    tf_begin("cmd/定义与参数解析");

    /* 位置参数 + 默认值 */
    {
        char *argv[] = { "example.com" };

        TF_CHECK_INT(PARSE(argv, 1, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_str(&p, 0U, &s) && strcmp(s, "example.com") == 0);
        TF_CHECK(rs_args_get_uint(&p, 1U, &u) && u == 4U);
        TF_CHECK(rs_args_get_bool(&p, 3U, &b) && !b);
    }

    /* 长选项 + 短选项 + 开关 */
    {
        char *argv[] = { "host", "--count", "10", "-v" };

        TF_CHECK_INT(PARSE(argv, 4, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_uint(&p, 1U, &u) && u == 10U);
        TF_CHECK(rs_args_get_bool(&p, 3U, &b) && b);
        TF_CHECK(!rs_args_get_bool(&p, 1U, &b));    /* UINT 值不能按布尔解析 */
    }

    /* --name=value 形式 */
    {
        char *argv[] = { "host", "--count=7", "--timeout=500" };

        TF_CHECK_INT(PARSE(argv, 3, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_uint(&p, 1U, &u) && u == 7U);
        TF_CHECK(rs_args_get_uint(&p, 2U, &u) && u == 500U);
    }

    /* 短选项取值 */
    {
        char *argv[] = { "host", "-c", "3" };

        TF_CHECK_INT(PARSE(argv, 3, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_uint(&p, 1U, &u) && u == 3U);
    }

    /* -- 终止符：后续按位置参数 */
    {
        char *argv[] = { "--", "--count" };

        TF_CHECK_INT(PARSE(argv, 2, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_str(&p, 0U, &s) && strcmp(s, "--count") == 0);
        TF_CHECK(rs_args_get_uint(&p, 1U, &u) && u == 4U);
    }

    /* 错误路径 */
    {
        char *empty[] = { 0 };
        char *unknown[] = { "host", "--bad", "1" };
        char *noval[] = { "host", "--count" };
        char *badval[] = { "host", "--count", "abc" };
        char *badbool[] = { "host", "--verbose=maybe" };
        char *extra[] = { "host", "extra" };

        TF_CHECK_INT(PARSE(empty, 0, &p), RS_ARGS_ERR_MISSING_ARG);
        TF_CHECK_INT(PARSE(unknown, 3, &p), RS_ARGS_ERR_UNKNOWN);
        TF_CHECK_INT(PARSE(noval, 2, &p), RS_ARGS_ERR_MISSING_VALUE);
        TF_CHECK_INT(PARSE(badval, 3, &p), RS_ARGS_ERR_TYPE);
        TF_CHECK_INT(PARSE(badbool, 2, &p), RS_ARGS_ERR_TYPE);
        TF_CHECK_INT(PARSE(extra, 2, &p), RS_ARGS_ERR_EXTRA);
        TF_CHECK_INT(rs_args_parse(NULL, 0U, NULL, 0, NULL), RS_ARGS_ERR_PARAM);
    }

    /* 查找与错误文本 */
    TF_CHECK_INT(rs_args_find(s_ping_def.args, s_ping_def.arg_count, "count"), 1);
    TF_CHECK_INT(rs_args_find_short(s_ping_def.args, s_ping_def.arg_count, 'v'), 3);
    TF_CHECK_INT(rs_args_find(s_ping_def.args, s_ping_def.arg_count, "nope"), -1);
    TF_CHECK(strcmp(rs_args_error_str(RS_ARGS_ERR_TYPE), "invalid value type") == 0);

    /* usage / help 文本 */
    TF_CHECK(rs_cmd_usage(&s_ping_def, buf, sizeof(buf)) > 0);
    TF_CHECK(strstr(buf, "ping <host>") != NULL);
    TF_CHECK(strstr(buf, "[--count <n>]") != NULL);
    TF_CHECK(strstr(buf, "[--verbose]") != NULL);
    TF_CHECK(rs_cmd_help(&s_ping_def, buf, sizeof(buf)) > 0);
    TF_CHECK(strstr(buf, "send ICMP echo") != NULL);
    TF_CHECK(strstr(buf, "default: 4") != NULL);

    /* 命令表分发 */
    {
        char *argv[] = { "ping", "host", "-c", "9" };
        char *miss[] = { "nope" };

        s_run_called = 0;
        s_run_value = 0;
        TF_CHECK_INT(rs_cmd_run(NULL, s_table, 1U, 4, argv, NULL), 42);
        TF_CHECK_INT(s_run_called, 1);
        TF_CHECK_INT(s_run_value, 9);
        TF_CHECK_INT(rs_cmd_run(NULL, s_table, 1U, 1, miss, NULL), RS_ARGS_ERR_UNKNOWN);
        TF_CHECK_INT(rs_cmd_run(NULL, s_table, 1U, 0, NULL, NULL), RS_ARGS_ERR_PARAM);
    }

    tf_end();

    /* ---------------- 生命周期钩子 ---------------- */
    tf_begin("cmd/生命周期钩子");
    {
        const rs_cmd_t *node;
        ros_task_t fake;
        ros_task_t *saved;
        char *hook_argv[] = { "hookcmd" };
        char *deny_argv[] = { "denycmd" };

        rs_cmd_unregister_all();
        TF_CHECK_INT(rs_cmd_register(&s_hook_def), 0);
        TF_CHECK_INT(rs_cmd_register(&s_deny_def), 0);

        node = rs_cmd_find_command("hookcmd");
        TF_CHECK(node != NULL);
        TF_CHECK(node->on_pause == hook_pause);
        TF_CHECK(node->on_resume == hook_resume);
        TF_CHECK(node->on_stop == hook_kill);
        node = rs_cmd_find_command("denycmd");
        TF_CHECK(node != NULL && node->on_pause == deny_pause);
        TF_CHECK(node->on_resume == NULL && node->on_stop == NULL);

        /* 无任务/无登记：安全返回 0 */
        TF_CHECK_INT(rs_cmd_notify_pause(NULL), 0);
        TF_CHECK_INT(rs_cmd_notify_resume(NULL), 0);
        TF_CHECK_INT(rs_cmd_notify_kill(NULL), 0);

        /* 用一个假任务模拟“命令正在执行”（结束后恢复真实当前任务） */
        saved = ros_current_task;
        memset(&fake, 0, sizeof(fake));
        s_hook_pause_calls = 0;
        s_hook_resume_calls = 0;
        s_hook_kill_calls = 0;
        ros_current_task = &fake;

        TF_CHECK_INT(rs_builtin_dispatch(NULL, 1, hook_argv), 0);
        TF_CHECK_INT(s_hook_pause_calls, 1);
        TF_CHECK_INT(s_hook_resume_calls, 1);
        TF_CHECK_INT(s_hook_kill_calls, 1);
        /* 命令返回后登记已弹出 */
        TF_CHECK_INT(rs_cmd_notify_pause(&fake), 0);

        /* 钩子拒绝暂停：返回负错误码 */
        TF_CHECK_INT(rs_builtin_dispatch(NULL, 1, deny_argv), 0);

        ros_current_task = saved;
        rs_cmd_unregister_all();
    }
    tf_end();
}

/* ---------------- 参数解析单元测试（原 args 测试） ----------------
 *
 * 覆盖：零参数命令、位置参数/必填、选项与开关、默认值取值、
 *       查找接口、错误码与文本、非法入参边界。
 */

/* 常规参数表：位置参数 + 长/短选项 + 布尔开关 */
static const rs_args_t s_args[] = {
    { "host",    0,   RS_ARGS_TYPE_STR,  true,  true,  NULL,  "target host" },
    { "count",   'c', RS_ARGS_TYPE_UINT, false, false, "4",   "count" },
    { "delay",   'd', RS_ARGS_TYPE_INT,  false, false, "-1",  "delay" },
    { "verbose", 'v', RS_ARGS_TYPE_BOOL, false, false, NULL,  "verbose" }
};
#define S_ARG_COUNT ((uint8_t)(sizeof(s_args) / sizeof(s_args[0])))

/* 各类型默认值（x 的默认值无法解析为整数，用于验证失败返回） */
static const rs_args_t s_def_args[] = {
    { "s", 0, RS_ARGS_TYPE_STR,  true, false, "hello", NULL },
    { "i", 0, RS_ARGS_TYPE_INT,  true, false, "-7",    NULL },
    { "u", 0, RS_ARGS_TYPE_UINT, true, false, "9",     NULL },
    { "b", 0, RS_ARGS_TYPE_BOOL, true, false, "on",    NULL },
    { "x", 0, RS_ARGS_TYPE_INT,  true, false, "oops",  NULL }
};
#define S_DEF_COUNT ((uint8_t)(sizeof(s_def_args) / sizeof(s_def_args[0])))

/* required=true 且带默认值：默认值不能替代必填 */
static const rs_args_t s_req_args[] = {
    { "req", 0, RS_ARGS_TYPE_STR, true, true, "dv", NULL }
};

/* 浮点参数：位置参数 + 带默认值的短选项 */
static const rs_args_t s_float_args[] = {
    { "level", 0,   RS_ARGS_TYPE_FLOAT, true,  true,  NULL,  "level" },
    { "gain",  'g', RS_ARGS_TYPE_FLOAT, false, false, "1.5", "gain" }
};
#define S_FLOAT_COUNT ((uint8_t)(sizeof(s_float_args) / sizeof(s_float_args[0])))

void ut_args(void)
{
    rs_args_parsed_t p;
    const char *s = NULL;
    const char *so = NULL;
    unsigned u = 0U;
    int i = 0;
    bool b = false;

    /* ---------------- 零参数命令（quit/exit 回归） ---------------- */
    tf_begin("args/零参数命令");
    {
        char *extra[] = { "x" };

        TF_CHECK_INT(rs_args_parse(NULL, 0U, NULL, 0, &p), RS_ARGS_OK);
        TF_CHECK(p.args == NULL);
        TF_CHECK_INT(p.arg_count, 0);
        TF_CHECK_INT(p.positionals, 0);
        TF_CHECK_INT(p.error, RS_ARGS_OK);

        /* 零参数命令仍拒绝多余位置参数 */
        TF_CHECK_INT(rs_args_parse(NULL, 0U, extra, 1, &p), RS_ARGS_ERR_EXTRA);
        TF_CHECK_INT(p.error, RS_ARGS_ERR_EXTRA);

        /* args 为 NULL 但声明了参数个数：非法 */
        TF_CHECK_INT(rs_args_parse(NULL, 1U, NULL, 0, &p), RS_ARGS_ERR_PARAM);
    }
    tf_end();

    /* ---------------- 位置参数与必填 ---------------- */
    tf_begin("args/位置参数与必填");
    {
        char *one[] = { "box" };
        char *two[] = { "box", "extra" };
        char *dash[] = { "-" };
        char *has_null[] = { "box", NULL };

        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, one, 1, &p), RS_ARGS_OK);
        TF_CHECK(p.args == s_args);
        TF_CHECK_INT(p.arg_count, S_ARG_COUNT);
        TF_CHECK(rs_args_get_str(&p, 0U, &s) && strcmp(s, "box") == 0);
        TF_CHECK(rs_args_get_uint(&p, 1U, &u) && u == 4U);
        TF_CHECK(rs_args_get_int(&p, 2U, &i) && i == -1);
        TF_CHECK(rs_args_get_bool(&p, 3U, &b) && !b);

        /* 必填缺失 */
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, NULL, 0, &p),
                     RS_ARGS_ERR_MISSING_ARG);
        TF_CHECK_INT(p.error, RS_ARGS_ERR_MISSING_ARG);
        TF_CHECK(strcmp(p.err_arg, "host") == 0);

        /* 多余位置参数 */
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, two, 2, &p),
                     RS_ARGS_ERR_EXTRA);

        /* 单独 "-" 不是选项，按位置参数处理 */
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, dash, 1, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_str(&p, 0U, &s) && strcmp(s, "-") == 0);

        /* argv 中间出现 NULL */
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, has_null, 2, &p),
                     RS_ARGS_ERR_PARAM);
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

        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, longv, 5, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_uint(&p, 1U, &u) && u == 10U);
        TF_CHECK(rs_args_get_int(&p, 2U, &i) && i == 5);
        TF_CHECK(rs_args_get_bool(&p, 3U, &b) && b);

        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, shortv, 6, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_uint(&p, 1U, &u) && u == 3U);
        TF_CHECK(rs_args_get_int(&p, 2U, &i) && i == -2);
        TF_CHECK(rs_args_get_bool(&p, 3U, &b) && b);

        /* 布尔开关带值 */
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, bool_on, 2, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_bool(&p, 3U, &b) && b);
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, bool_off, 2, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_bool(&p, 3U, &b) && !b);

        /* 错误路径 */
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, unknown, 3, &p),
                     RS_ARGS_ERR_UNKNOWN);
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, unknown_short, 2, &p),
                     RS_ARGS_ERR_UNKNOWN);
        TF_CHECK(strcmp(p.err_arg, "-z") == 0);
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, noval, 2, &p),
                     RS_ARGS_ERR_MISSING_VALUE);
        TF_CHECK(strcmp(p.err_arg, "count") == 0);
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, badval, 3, &p),
                     RS_ARGS_ERR_TYPE);
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, badbool, 2, &p),
                     RS_ARGS_ERR_TYPE);

        /* "--" 之后全部按位置参数 */
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, term, 2, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_str(&p, 0U, &s) && strcmp(s, "--count") == 0);
        TF_CHECK(rs_args_get_uint(&p, 1U, &u) && u == 4U);
    }
    tf_end();

    /* ---------------- 取值接口与默认值 ---------------- */
    tf_begin("args/取值接口与默认值");
    {
        TF_CHECK_INT(rs_args_parse(s_def_args, S_DEF_COUNT, NULL, 0, &p), RS_ARGS_OK);
        TF_CHECK(rs_args_get_str(&p, 0U, &s) && strcmp(s, "hello") == 0);
        TF_CHECK(rs_args_get_int(&p, 1U, &i) && i == -7);
        TF_CHECK(rs_args_get_uint(&p, 2U, &u) && u == 9U);
        TF_CHECK(rs_args_get_bool(&p, 3U, &b) && b);

        /* 默认值类型不匹配 / 类型交叉读取 */
        TF_CHECK(!rs_args_get_int(&p, 4U, &i));
        TF_CHECK(!rs_args_get_uint(&p, 0U, &u));
        TF_CHECK(!rs_args_get_int(&p, 0U, &i));

        /* 下标越界、输出指针为空、parsed 为空 */
        TF_CHECK(!rs_args_get_str(&p, 20U, &s));
        TF_CHECK(!rs_args_get_int(&p, 1U, NULL));
        TF_CHECK(!rs_args_get_str(&p, 0U, NULL));
        TF_CHECK(!rs_args_get_uint(&p, 2U, NULL));
        TF_CHECK(!rs_args_get_bool(&p, 3U, NULL));
        TF_CHECK(!rs_args_get_str(NULL, 0U, &so));
        TF_CHECK(!rs_args_get_int(NULL, 0U, &i));
        TF_CHECK(!rs_args_get_uint(NULL, 0U, &u));
        TF_CHECK(!rs_args_get_bool(NULL, 0U, &b));

        /* required=true + 默认值：默认值不能满足必填 */
        TF_CHECK_INT(rs_args_parse(s_req_args, 1U, NULL, 0, &p),
                     RS_ARGS_ERR_MISSING_ARG);
        TF_CHECK(strcmp(p.err_arg, "req") == 0);
        {
            char *v[] = { "val" };

            TF_CHECK_INT(rs_args_parse(s_req_args, 1U, v, 1, &p), RS_ARGS_OK);
            TF_CHECK(rs_args_get_str(&p, 0U, &s) && strcmp(s, "val") == 0);
        }
    }
    tf_end();

    /* ---------------- 浮点参数 ---------------- */
    tf_begin("args/浮点参数");
    {
        char *plain[] = { "2.25" };
        char *opts[] = { "0.5", "--gain=-3.75" };
        char *expv[] = { "1e2", "-g", "-2.5e-1" };
        char *bad[] = { "abc" };
        char *badopt[] = { "1.0", "--gain=x" };
        char *nolevel[] = { "--gain", "2.5" };
        char *empty[] = { "" };
        float f = 0.0f;

        /* 位置参数取值 + 选项默认值回退 */
        TF_CHECK_INT(rs_args_parse(s_float_args, S_FLOAT_COUNT, plain, 1, &p),
                     RS_ARGS_OK);
        TF_CHECK(rs_args_get_float(&p, 0U, &f) && f == 2.25f);
        TF_CHECK(rs_args_get_float(&p, 1U, &f) && f == 1.5f);

        /* 负数与 --name=value */
        TF_CHECK_INT(rs_args_parse(s_float_args, S_FLOAT_COUNT, opts, 2, &p),
                     RS_ARGS_OK);
        TF_CHECK(rs_args_get_float(&p, 0U, &f) && f == 0.5f);
        TF_CHECK(rs_args_get_float(&p, 1U, &f) && f == -3.75f);

        /* e 指数与指数负号 */
        TF_CHECK_INT(rs_args_parse(s_float_args, S_FLOAT_COUNT, expv, 3, &p),
                     RS_ARGS_OK);
        TF_CHECK(rs_args_get_float(&p, 0U, &f) && f == 100.0f);
        TF_CHECK(rs_args_get_float(&p, 1U, &f) && f == -0.25f);

        /* 非法取值（位置参数 / 选项 / 空串） */
        TF_CHECK_INT(rs_args_parse(s_float_args, S_FLOAT_COUNT, bad, 1, &p),
                     RS_ARGS_ERR_TYPE);
        TF_CHECK_INT(rs_args_parse(s_float_args, S_FLOAT_COUNT, badopt, 2, &p),
                     RS_ARGS_ERR_TYPE);
        TF_CHECK(strcmp(p.err_arg, "gain") == 0);
        TF_CHECK_INT(rs_args_parse(s_float_args, S_FLOAT_COUNT, empty, 1, &p),
                     RS_ARGS_ERR_TYPE);

        /* 必填位置参数缺失 */
        TF_CHECK_INT(rs_args_parse(s_float_args, S_FLOAT_COUNT, nolevel, 2, &p),
                     RS_ARGS_ERR_MISSING_ARG);
        TF_CHECK(strcmp(p.err_arg, "level") == 0);

        /* 交叉读取：浮点文本不能按整数解析，字符串接口返回原文 */
        TF_CHECK_INT(rs_args_parse(s_float_args, S_FLOAT_COUNT, plain, 1, &p),
                     RS_ARGS_OK);
        TF_CHECK(!rs_args_get_int(&p, 0U, &i));
        TF_CHECK(rs_args_get_str(&p, 0U, &s) && strcmp(s, "2.25") == 0);

        /* 空输出指针、空 parsed、下标越界 */
        TF_CHECK(!rs_args_get_float(&p, 0U, NULL));
        TF_CHECK(!rs_args_get_float(NULL, 0U, &f));
        TF_CHECK(!rs_args_get_float(&p, 20U, &f));
    }
    tf_end();

    /* ---------------- 查找与错误文本 ---------------- */
    tf_begin("args/查找与错误文本");
    {
        static const struct {
            int code;
            const char *text;
        } errors[] = {
            { RS_ARGS_OK,                "ok" },
            { RS_ARGS_ERR_PARAM,         "invalid parameter" },
            { RS_ARGS_ERR_UNKNOWN,       "unknown option" },
            { RS_ARGS_ERR_MISSING_VALUE, "missing option value" },
            { RS_ARGS_ERR_MISSING_ARG,   "missing required argument" },
            { RS_ARGS_ERR_TYPE,          "invalid value type" },
            { RS_ARGS_ERR_EXTRA,         "unexpected argument" }
        };
        uint32_t k;

        TF_CHECK_INT(rs_args_find(s_args, S_ARG_COUNT, "count"), 1);
        TF_CHECK_INT(rs_args_find(s_args, S_ARG_COUNT, "verbose"), 3);
        TF_CHECK_INT(rs_args_find(s_args, S_ARG_COUNT, "nope"), -1);
        TF_CHECK_INT(rs_args_find(s_args, S_ARG_COUNT, NULL), -1);
        TF_CHECK_INT(rs_args_find(NULL, 0U, "count"), -1);

        TF_CHECK_INT(rs_args_find_short(s_args, S_ARG_COUNT, 'd'), 2);
        TF_CHECK_INT(rs_args_find_short(s_args, S_ARG_COUNT, 'z'), -1);
        TF_CHECK_INT(rs_args_find_short(s_args, S_ARG_COUNT, '\0'), -1);
        TF_CHECK_INT(rs_args_find_short(NULL, 0U, 'v'), -1);

        for (k = 0U; k < sizeof(errors) / sizeof(errors[0]); k++) {
            TF_CHECK(strcmp(rs_args_error_str(errors[k].code),
                            errors[k].text) == 0);
        }
        TF_CHECK(strcmp(rs_args_error_str(999), "command error") == 0);
    }
    tf_end();

    /* ---------------- 非法入参边界 ---------------- */
    tf_begin("args/非法入参");
    {
        char *one[] = { "box" };

        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, NULL, 0, NULL),
                     RS_ARGS_ERR_PARAM);
        TF_CHECK_INT(rs_args_parse(s_args, (uint8_t)(RS_ARGS_MAX + 1U),
                                NULL, 0, &p), RS_ARGS_ERR_PARAM);
        TF_CHECK_INT(rs_args_parse(NULL, 0U, NULL, 0, NULL), RS_ARGS_ERR_PARAM);
        TF_CHECK_INT(rs_args_parse(s_args, S_ARG_COUNT, one, 0, &p),
                     RS_ARGS_ERR_MISSING_ARG);
    }
    tf_end();
}
