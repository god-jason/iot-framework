/*
 * shell_test.c - IoT Shell V1.1 回归测试
 *
 * 通过 shell_exec_line / shell_exec_script 同步驱动，内部 API 直测词法、
 * 变量表与 JOB 生命周期。覆盖：
 *   初始化、词法、单位/浮点、表达式与短路、变量表、
 *   echo/print、控制语句、ON ERROR/ON STOP、JOB 生命周期、
 *   命令错误路径、扩展命令注册、输入缓冲边界。
 */

#include "test.h"
#include "shell_internal.h"

#include <string.h>
#include <stdio.h>

#define OUT_MAX 8192

static char s_out[OUT_MAX];
static uint32_t s_out_len;

static int capture_write(const char *data, uint32_t len, void *user)
{
    (void)user;
    if (s_out_len + len >= OUT_MAX) {
        len = OUT_MAX - s_out_len - 1U;
    }
    memcpy(&s_out[s_out_len], data, len);
    s_out_len += len;
    s_out[s_out_len] = '\0';
    return (int)len;
}

static void out_reset(void)
{
    s_out_len = 0U;
    s_out[0] = '\0';
}

static bool out_has(const char *text)
{
    return strstr(s_out, text) != NULL;
}

static void shell_reset_all(void)
{
    shell_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test-build";
    cfg.write = capture_write;
    (void)shell_setup(&cfg);
    out_reset();
}

/* ------------------------------ 扩展命令 ------------------------------ */

static int ext_ok_handler(int argc, char **argv)
{
    (void)argv;
    shell_printf("ext argc=%d\n", argc);
    return SHELL_OK;
}

static int ext_fail_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return -SHELL_ERR_DEVICE;
}

static const shell_cmd_t s_ext_ok = {
    "ext", ext_ok_handler, "ext [a]", "ext help", "test", CMD_NONE
};
static const shell_cmd_t s_ext_danger = {
    "boom", ext_fail_handler, "boom", "", "test", CMD_DANGER
};
static const shell_cmd_t s_ext_dup = {
    "ver", ext_ok_handler, "ver x", "", "test", CMD_NONE
};
static const shell_cmd_t s_ext_pool[9] = {
    { "e0", ext_ok_handler, "e0", "", "t", CMD_NONE },
    { "e1", ext_ok_handler, "e1", "", "t", CMD_NONE },
    { "e2", ext_ok_handler, "e2", "", "t", CMD_NONE },
    { "e3", ext_ok_handler, "e3", "", "t", CMD_NONE },
    { "e4", ext_ok_handler, "e4", "", "t", CMD_NONE },
    { "e5", ext_ok_handler, "e5", "", "t", CMD_NONE },
    { "e6", ext_ok_handler, "e6", "", "t", CMD_NONE },
    { "e7", ext_ok_handler, "e7", "", "t", CMD_NONE },
    { "e8", ext_ok_handler, "e8", "", "t", CMD_NONE }
};

static void test_init_and_lex(void)
{
    shell_config_t cfg;
    shell_tok_t toks[16];
    char buf[16];

    tf_begin("shell/初始化与词法");

    TF_CHECK_INT(shell_setup(NULL), -1);
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "bad";
    TF_CHECK_INT(shell_setup(&cfg), -1);        /* 缺 write 后端 */

    shell_reset_all();

    /* 注释：; 与 // 在 token 边界截断 */
    TF_CHECK_INT(shell_lex("PRINT 1 + 2 ; tail",
                           (uint16_t)strlen("PRINT 1 + 2 ; tail"), toks, 16U), 5);
    TF_CHECK_INT(toks[0].type, SHELL_TOK_WORD);
    TF_CHECK_INT(toks[1].type, SHELL_TOK_NUMBER);
    TF_CHECK_INT(toks[1].ival, 1);
    TF_CHECK_INT(toks[3].ival, 2);
    TF_CHECK_INT(toks[4].type, SHELL_TOK_EOF);

    TF_CHECK_INT(shell_lex("// only comment",
                           (uint16_t)strlen("// only comment"), toks, 16U), 1);
    TF_CHECK_INT(toks[0].type, SHELL_TOK_EOF);

    /* 逗号等同空白 */
    TF_CHECK_INT(shell_lex("1,2", 3U, toks, 16U), 3);
    TF_CHECK_INT(toks[0].ival, 1);
    TF_CHECK_INT(toks[1].ival, 2);

    /* 字符串 token 保留引号；未闭合只留开引号 */
    TF_CHECK_INT(shell_lex("\"hi\"", 4U, toks, 16U), 2);
    TF_CHECK_INT(toks[0].type, SHELL_TOK_STRING);
    TF_CHECK_INT(toks[0].len, 4);
    TF_CHECK(toks[0].p[0] == '"');
    TF_CHECK_INT(shell_lex("\"oops", 5U, toks, 16U), 2);
    TF_CHECK_INT(toks[0].len, 1);

    /* 变量 token 含 $ */
    TF_CHECK_INT(shell_lex("$SPEED", 6U, toks, 16U), 2);
    TF_CHECK_INT(toks[0].type, SHELL_TOK_VAR);
    TF_CHECK_INT(toks[0].len, 6);
    TF_CHECK(toks[0].p[0] == '$');

    /* 非法参数 */
    TF_CHECK_INT(shell_lex(NULL, 0U, toks, 16U), 0);
    TF_CHECK_INT(shell_lex("x", 1U, NULL, 16U), 0);
    TF_CHECK_INT(shell_lex("x", 1U, toks, 1U), 0);

    /* 值格式化 */
    {
        shell_value_t v;
        uint16_t n = 0U;

        memset(&v, 0, sizeof(v));
        v.type = SHELL_VAL_INT;
        v.i = -42;
        shell_value_format(&v, buf, sizeof(buf), &n);
        TF_CHECK_STR(buf, "-42");
        TF_CHECK_INT(n, 3);

        v.type = SHELL_VAL_BOOL;
        v.b = true;
        shell_value_format(&v, buf, sizeof(buf), &n);
        TF_CHECK_STR(buf, "TRUE");

        v.type = SHELL_VAL_STRING;
        v.s = "abc";
        v.slen = 3U;
        shell_value_format(&v, buf, sizeof(buf), &n);
        TF_CHECK_STR(buf, "abc");
    }

    tf_end();
}

static void test_expr(void)
{
    tf_begin("shell/表达式与短路");

    shell_reset_all();

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 2 + 3 * 4"), 0);
    TF_CHECK(out_has("14\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print (2 + 3) * 4"), 0);
    TF_CHECK(out_has("20\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 10 - 2 - 3"), 0);
    TF_CHECK(out_has("5\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 0 - -3"), 0);
    TF_CHECK(out_has("3\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 5 % 3"), 0);
    TF_CHECK(out_has("2\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 10 / 4"), 0);
    TF_CHECK(out_has("2\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 10.0 / 4"), 0);
    TF_CHECK(out_has("2.5\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1.5 + 2.25"), 0);
    TF_CHECK(out_has("3.75\n"));

    /* 关系 / 相等 / 逻辑 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 2 < 3"), 0);
    TF_CHECK(out_has("TRUE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 3 <= 3"), 0);
    TF_CHECK(out_has("TRUE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 4 > 5"), 0);
    TF_CHECK(out_has("FALSE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1 == 1"), 0);
    TF_CHECK(out_has("TRUE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1 != 2"), 0);
    TF_CHECK(out_has("TRUE"));

    /* 布尔与数字混用比较 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("print TRUE == FALSE"), 0);
    TF_CHECK(out_has("FALSE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1 == TRUE"), 0);
    TF_CHECK(out_has("TRUE"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print NOT 0"), 0);
    TF_CHECK(out_has("TRUE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1 AND 0"), 0);
    TF_CHECK(out_has("FALSE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 0 OR 1"), 0);
    TF_CHECK(out_has("TRUE"));

    /* 短路：右侧不参与求值，因此不会触发除零 / 未定义变量 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 0 AND (1 / 0)"), 0);
    TF_CHECK(out_has("FALSE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1 OR $UNDEF"), 0);
    TF_CHECK(out_has("TRUE"));

    /* 字符串比较区分大小写 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("print \"HI\" == \"HI\""), 0);
    TF_CHECK(out_has("TRUE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print \"HI\" == \"hi\""), 0);
    TF_CHECK(out_has("FALSE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print TRUE"), 0);
    TF_CHECK(out_has("TRUE"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("print OFF"), 0);
    TF_CHECK(out_has("FALSE"));

    /* 语法 / 类型错误 */
    TF_CHECK_INT(shell_exec_line("print 1 / 0"), -SHELL_ERR_DIV_ZERO);
    TF_CHECK_INT(shell_exec_line("print 7 % 0"), -SHELL_ERR_DIV_ZERO);
    TF_CHECK_INT(shell_exec_line("print $UNDEF + 1"), -SHELL_ERR_UNDEF_VAR);
    TF_CHECK_INT(shell_exec_line("print \"A\" + 1"), -SHELL_ERR_TYPE);
    TF_CHECK_INT(shell_exec_line("print \"A\" == 1"), -SHELL_ERR_TYPE);
    TF_CHECK_INT(shell_exec_line("print TRUE + 1"), -SHELL_ERR_TYPE);
    TF_CHECK_INT(shell_exec_line("print"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("print 1 +"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("print (1 + 2"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("print 1 2"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("print $"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("print \"abc"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("print 3 = 3"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("print NOPE"), -SHELL_ERR_PARSE);

    tf_end();
}

static void test_units(void)
{
    tf_begin("shell/单位与浮点");

    shell_reset_all();

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 300ms"), 0);
    TF_CHECK(out_has("300\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 2s"), 0);
    TF_CHECK(out_has("2000\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1min"), 0);
    TF_CHECK(out_has("60000\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 2kHz"), 0);
    TF_CHECK(out_has("2000\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 5%"), 0);
    TF_CHECK(out_has("5\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1.5s"), 0);
    TF_CHECK(out_has("1500\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1e3"), 0);
    TF_CHECK(out_has("1000\n"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print 2.5e-2"), 0);
    TF_CHECK(out_has("0.025\n"));

    /* 单位参与运算后归一 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("print 1s + 500ms"), 0);
    TF_CHECK(out_has("1500\n"));

    tf_end();
}

static void test_vars(void)
{
    tf_begin("shell/变量表");

    shell_reset_all();

    /* 名字大小写折叠 */
    TF_CHECK_INT(shell_exec_line("set $speed = 10"), 0);
    out_reset();
    TF_CHECK_INT(shell_exec_line("print $SPEED"), 0);
    TF_CHECK(out_has("10\n"));

    /* SET 无空格写法 */
    TF_CHECK_INT(shell_exec_line("set $X=7"), 0);
    out_reset();
    TF_CHECK_INT(shell_exec_line("print $x"), 0);
    TF_CHECK(out_has("7\n"));

    /* 字符串与转义 */
    TF_CHECK_INT(shell_exec_line("set $S = \"A\\\"B\""), 0);
    out_reset();
    TF_CHECK_INT(shell_exec_line("print $S"), 0);
    TF_CHECK(out_has("A\"B"));

    /* 未定义变量 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("echo $NOPE"), 0);
    TF_CHECK(out_has("WARN: undefined variable"));
    TF_CHECK(out_has("$NOPE"));

    /* unset */
    TF_CHECK_INT(shell_exec_line("unset $SPEED"), 0);
    TF_CHECK_INT(shell_exec_line("print $SPEED"), -SHELL_ERR_UNDEF_VAR);
    TF_CHECK_INT(shell_exec_line("unset $SPEED"), -SHELL_ERR_NOT_FOUND);
    TF_CHECK_INT(shell_exec_line("unset"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("unset speed"), -SHELL_ERR_PARAM);

    /* SET 语法错误 */
    TF_CHECK_INT(shell_exec_line("set 1 = 2"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("set $X 5"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("set $X = 1/0"), -SHELL_ERR_DIV_ZERO);

    /* 变量表容量边界 */
    {
        char line[48];
        int i;

        shell_reset_all();
        for (i = 0; i < (int)SHELL_VAR_MAX; i++) {
            (void)snprintf(line, sizeof(line), "set $V%d = %d", i, i);
            TF_CHECK_INT(shell_exec_line(line), 0);
        }
        TF_CHECK_INT(shell_exec_line("set $VLAST = 1"), -SHELL_ERR_RESOURCE);
    }

    /* 只读变量：内部 API */
    {
        shell_value_t v;
        shell_err_t err;
        int32_t ival = 0;

        shell_reset_all();
        memset(&v, 0, sizeof(v));
        memset(&err, 0, sizeof(err));
        v.type = SHELL_VAL_INT;
        v.i = 1;
        TF_CHECK_INT(shell_vars_set(&g_shell_vars, "RO", 2U, &v, true, &err), 0);
        TF_CHECK_INT(shell_vars_set(&g_shell_vars, "RO", 2U, &v, true, &err), -1);
        TF_CHECK_INT(err.code, SHELL_ERR_READONLY);
        TF_CHECK_INT(shell_vars_unset(&g_shell_vars, "RO", 2U), -2);

        TF_CHECK_INT(shell_value_to_int(&v, &ival, &err), 0);
        TF_CHECK_INT(ival, 1);

        v.type = SHELL_VAL_STRING;
        v.s = "abc";
        v.slen = 3U;
        TF_CHECK_INT(shell_value_to_int(&v, &ival, &err), -1);
        TF_CHECK_INT(err.code, SHELL_ERR_TYPE);
    }

    tf_end();
}

static void test_echo_print(void)
{
    tf_begin("shell/echo 与 print");

    shell_reset_all();
    TF_CHECK_INT(shell_exec_line("set $SPEED = 1000"), 0);

    out_reset();
    TF_CHECK_INT(shell_exec_line("echo $SPEED"), 0);
    TF_CHECK(out_has("1000"));

    /* echo 不求值 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("echo 1 + 2"), 0);
    TF_CHECK(out_has("1 + 2"));

    /* 引号包裹的空格保持原样 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("echo \"a  b\""), 0);
    TF_CHECK(out_has("a  b"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("echo one two"), 0);
    TF_CHECK(out_has("one two"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("echo \"x\" \"y\""), 0);
    TF_CHECK(out_has("x y"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("print \"hello world\""), 0);
    TF_CHECK(out_has("hello world"));

    tf_end();
}

static void test_control(void)
{
    char script[4096];
    int i;

    tf_begin("shell/控制语句");

    shell_reset_all();

    /* IF / ELSE 嵌套 */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $X = 2\n"
        "IF $X == 1\n"
        "PRINT \"alpha\"\n"
        "ELSE\n"
        "IF $X == 2\n"
        "PRINT \"beta\"\n"
        "ELSE\n"
        "PRINT \"gamma\"\n"
        "END\n"
        "END\n"), 0);
    TF_CHECK(out_has("beta"));
    TF_CHECK(!out_has("alpha"));
    TF_CHECK(!out_has("gamma"));

    /* FOR：结束后 $I = 10（首次越界值） */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $SUM = 0\n"
        "FOR $I = 0 TO 9\n"
        "SET $SUM = $SUM + $I\n"
        "ENDFOR\n"
        "PRINT $I\n"
        "PRINT $SUM\n"), 0);
    TF_CHECK(out_has("10\n"));
    TF_CHECK(out_has("45"));

    /* FOR 一次不进入：变量保持循环前值 */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $J = 7\n"
        "FOR $J = 5 TO 1\n"
        "SET $J = 99\n"
        "ENDFOR\n"
        "PRINT $J\n"), 0);
    TF_CHECK(out_has("7\n"));

    /* FOR 负步长 */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $SUM = 0\n"
        "FOR $I = 3 TO 1 STEP -1\n"
        "SET $SUM = $SUM + $I\n"
        "ENDFOR\n"
        "PRINT $I\n"
        "PRINT $SUM\n"), 0);
    TF_CHECK(out_has("0\n"));
    TF_CHECK(out_has("6"));

    /* LOOP N：固定次数，隐式 __I */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $S = 0\n"
        "LOOP 3\n"
        "SET $S = $S + 1\n"
        "ENDLOOP\n"
        "PRINT $S\n"), 0);
    TF_CHECK(out_has("3\n"));

    /* WHILE + BREAK（IF 内 BREAK 需要正确回填目标） */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $N = 0\n"
        "WHILE 1\n"
        "SET $N = $N + 1\n"
        "IF $N == 3\n"
        "BREAK\n"
        "END\n"
        "ENDWHILE\n"
        "PRINT $N\n"), 0);
    TF_CHECK(out_has("3\n"));

    /* WHILE + CONTINUE */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $N = 0\n"
        "SET $S = 0\n"
        "WHILE $N < 5\n"
        "SET $N = $N + 1\n"
        "IF $N == 3\n"
        "CONTINUE\n"
        "END\n"
        "SET $S = $S + 1\n"
        "ENDWHILE\n"
        "PRINT $S\n"), 0);
    TF_CHECK(out_has("4\n"));

    /* 嵌套循环：内层 BREAK 只跳出本层 */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $OUT = 0\n"
        "FOR $I = 1 TO 2\n"
        "FOR $J = 1 TO 3\n"
        "IF $J == 2\n"
        "BREAK\n"
        "END\n"
        "SET $OUT = $OUT + 1\n"
        "ENDFOR\n"
        "ENDFOR\n"
        "PRINT $OUT\n"), 0);
    TF_CHECK(out_has("2\n"));

    /* LABEL / GOTO */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $N = 0\n"
        "LABEL TOP\n"
        "SET $N = $N + 1\n"
        "IF $N < 3\n"
        "GOTO TOP\n"
        "END\n"
        "PRINT $N\n"), 0);
    TF_CHECK(out_has("3\n"));

    /* CALL：顺序跳转（无返回栈），调用点之后的语句被跳过 */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "CALL SUB\n"
        "PRINT \"after\"\n"
        "LABEL SUB\n"
        "PRINT \"sub\"\n"), 0);
    TF_CHECK(out_has("sub"));
    TF_CHECK(!out_has("after"));

    /* WAIT TIME 在同步执行下立即返回 */
    TF_CHECK_INT(shell_exec_script("WAIT TIME 100ms\n"), 0);

    /* 解析错误 */
    TF_CHECK_INT(shell_exec_script("IF 1\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("END\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("ENDFOR\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("ENDWHILE\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("ELSE\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("BREAK\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("CONTINUE\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("START FEED\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("WAIT 100\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("ON BREAK\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("LABEL\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("GOTO NOPE\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("LABEL A\nLABEL a\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script("BOGUS 1\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script(
        "FOR $I = 1\nPRINT $I\nENDFOR\n"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_script(
        "FOR $I = 1 TO 3 STEP 0\nPRINT $I\nENDFOR\n"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_script(
        "START JOB NOPE\n"), -SHELL_ERR_NOT_FOUND);
    TF_CHECK_INT(shell_exec_script(
        "STOP JOB NOPE\n"), -SHELL_ERR_NOT_FOUND);
    TF_CHECK_INT(shell_exec_script(
        "IF \"x\"\nPRINT 1\nEND\n"), -SHELL_ERR_TYPE);

    /* 块嵌套深度上限（8） */
    script[0] = '\0';
    for (i = 0; i < 9; i++) {
        (void)strcat(script, "IF 1\n");
    }
    for (i = 0; i < 9; i++) {
        (void)strcat(script, "END\n");
    }
    out_reset();
    TF_CHECK_INT(shell_exec_script(script), -SHELL_ERR_PARSE);
    TF_CHECK(out_has("block depth exceeded"));

    /* 指令数上限（128） */
    script[0] = '\0';
    for (i = 0; i < 130; i++) {
        (void)strcat(script, "PRINT 1\n");
    }
    out_reset();
    TF_CHECK_INT(shell_exec_script(script), -SHELL_ERR_PARSE);
    TF_CHECK(out_has("too many instructions"));

    tf_end();
}

static void test_on_error(void)
{
    tf_begin("shell/ON ERROR 与 ON STOP");

    shell_reset_all();
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "JOB T1\n"
        "ON ERROR\n"
        "SHELL PRINT \"err=\"\n"
        "SHELL PRINT $ERRCODE\n"
        "ENDERROR\n"
        "PRINT 1 / 0\n"
        "ENDJOB\n"), -SHELL_ERR_DIV_ZERO);
    TF_CHECK(out_has("err="));
    TF_CHECK(out_has("1011"));

    /* 错误变量：行号与消息 */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SET $A = 1\n"
        "ON ERROR\n"
        "SHELL PRINT $ERRLINE\n"
        "SHELL PRINT $ERRMSG\n"
        "ENDERROR\n"
        "PRINT 1 / 0\n"), -SHELL_ERR_DIV_ZERO);
    TF_CHECK(out_has("6\n"));
    TF_CHECK(out_has("divide by zero"));

    /* 正常结束同样执行 ON STOP（JOB cleanup） */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "JOB X\n"
        "ON STOP\n"
        "SHELL PRINT \"stopped\"\n"
        "ENDSTOP\n"
        "SHELL PRINT \"done\"\n"
        "ENDJOB\n"), 0);
    TF_CHECK(out_has("done"));
    TF_CHECK(out_has("stopped"));

    /* JOB 块未闭合 */
    TF_CHECK_INT(shell_exec_script("JOB X\nPRINT 1\n"), -SHELL_ERR_PARSE);

    /* SHELL 语句透传 */
    out_reset();
    TF_CHECK_INT(shell_exec_script("SHELL ver\n"), 0);
    TF_CHECK(out_has("IoT Shell v1.1"));

    out_reset();
    TF_CHECK_INT(shell_exec_script("SHELL PRINT \"x\"\n"), 0);
    TF_CHECK(out_has("[JOB @interactive] x"));

    /* SHELL STATE TO $VAR */
    out_reset();
    TF_CHECK_INT(shell_exec_script(
        "SHELL STATE TO $ST\n"
        "PRINT $ST\n"), 0);
    TF_CHECK(out_has("job=@interactive"));
    TF_CHECK(out_has("state=RUNNING"));

    tf_end();
}

static void test_jobs(void)
{
    shell_job_t *job;
    shell_err_t err;
    char *text = "PRINT 1\n";
    uint32_t len = (uint32_t)strlen(text);
    int i;

    tf_begin("shell/JOB 生命周期");

    shell_reset_all();

    /* 无 JOB */
    out_reset();
    TF_CHECK_INT(shell_exec_line("jobs"), 0);
    TF_CHECK(out_has("(no jobs)"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("loads"), 0);
    TF_CHECK(out_has("(no jobs)"));

    /* 加载 / 重名 / 保留名 / 非法名 / 空脚本 / 解析失败 */
    job = shell_job_load(text, len, "T1", 0U, 0U, &err);
    TF_CHECK(job != NULL);
    TF_CHECK_INT(job->state, JOB_LOADED);
    TF_CHECK_INT(job->prio, SHELL_JOB_PRIO);
    TF_CHECK_INT(job->stack, SHELL_JOB_STACK);
    TF_CHECK(shell_job_find("t1", 2U) == job);      /* 名字折叠 */
    TF_CHECK(shell_job_load(text, len, "T1", 0U, 0U, &err) == NULL);
    TF_CHECK_INT(err.code, SHELL_ERR_NAME);
    TF_CHECK(shell_job_load(text, len, "shell", 0U, 0U, &err) == NULL);
    TF_CHECK(shell_job_load(text, len, "IDLE", 0U, 0U, &err) == NULL);
    TF_CHECK(shell_job_load(text, len, "ABCDEFGHIJKLMNOPQ", 0U, 0U, &err) == NULL);
    TF_CHECK(shell_job_load("", 0U, "T2", 0U, 0U, &err) == NULL);
    TF_CHECK(shell_job_load("BOGUS\n", 6U, "T2", 0U, 0U, &err) == NULL);

    /* 已加载任务的列表 / 查看 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("loads"), 0);
    TF_CHECK(out_has("T1"));
    TF_CHECK(out_has("LOADED"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("job T1"), 0);
    TF_CHECK(out_has("Name    : T1"));

    /* 同步执行 → COMPLETED */
    TF_CHECK_INT(shell_job_run_sync_script(job, false), 0);
    TF_CHECK_INT(job->state, JOB_COMPLETED);

    out_reset();
    TF_CHECK_INT(shell_exec_line("loads"), 0);
    TF_CHECK(out_has("COMPLETED"));

    /* 已结束：fg / waitjob 立即返回，stop 拒绝 */
    TF_CHECK_INT(shell_exec_line("fg T1"), 0);
    TF_CHECK_INT(shell_exec_line("waitjob T1"), 0);
    TF_CHECK_INT(shell_exec_line("stop T1"), -SHELL_ERR_STATE);

    /* 卸载 */
    TF_CHECK_INT(shell_exec_line("unload T1"), 0);
    TF_CHECK(shell_job_find("T1", 2U) == NULL);
    TF_CHECK_INT(shell_exec_line("unload T1"), -SHELL_ERR_NOT_FOUND);

    /* 表满：第 9 个失败 */
    shell_reset_all();
    for (i = 0; i < (int)SHELL_JOB_MAX; i++) {
        char name[8];

        (void)snprintf(name, sizeof(name), "J%d", i);
        TF_CHECK(shell_job_load(text, len, name, 0U, 0U, &err) != NULL);
    }
    TF_CHECK(shell_job_load(text, len, "OVER", 0U, 0U, &err) == NULL);
    TF_CHECK_INT(err.code, SHELL_ERR_RESOURCE);
    out_reset();
    TF_CHECK_INT(shell_exec_line("loads"), 0);
    TF_CHECK(out_has("J7"));
    shell_jobs_clear();
    TF_CHECK(shell_job_find("J0", 2U) == NULL);

    /* shell_reset 清理 JOB 与变量 */
    TF_CHECK_INT(shell_exec_line("set $A = 1"), 0);
    TF_CHECK(shell_job_load(text, len, "R1", 0U, 0U, &err) != NULL);
    shell_reset();
    TF_CHECK(shell_job_find("R1", 2U) == NULL);
    TF_CHECK_INT(shell_exec_line("print $A"), -SHELL_ERR_UNDEF_VAR);

    tf_end();
}

static void test_cmd_errors(void)
{
    tf_begin("shell/命令与错误路径");

    shell_reset_all();

    /* 帮助 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("HELP"), 0);
    TF_CHECK(out_has("Commands:"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("help ps"), 0);
    TF_CHECK(out_has("ps [all"));
    out_reset();
    TF_CHECK_INT(shell_exec_line("help nosuchtopic"), 0);
    TF_CHECK(out_has("help topics:"));

    /* ps 分支 */
    out_reset();
    TF_CHECK_INT(shell_exec_line("PS"), 0);
    TF_CHECK(out_has("TID  NAME"));
    TF_CHECK(out_has("CPU%"));
    TF_CHECK_INT(shell_exec_line("ps -a"), 0);
    TF_CHECK_INT(shell_exec_line("ps job"), 0);
    TF_CHECK_INT(shell_exec_line("ps sys"), 0);

    out_reset();
    TF_CHECK_INT(shell_exec_line("state"), 0);
    TF_CHECK(out_has("RTOS Tasks:"));
    TF_CHECK(out_has("Shell Vars:"));

    /* 缺参数 */
    TF_CHECK_INT(shell_exec_line("wait"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("wait task"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("wait abc"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("wait task NOPE"), -SHELL_ERR_NOT_FOUND);
    TF_CHECK_INT(shell_exec_line("kill"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("kill NOPE"), -SHELL_ERR_NOT_FOUND);
    TF_CHECK_INT(shell_exec_line("kill #999"), -SHELL_ERR_NOT_FOUND);
    TF_CHECK_INT(shell_exec_line("pause"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("pause NOPE"), -SHELL_ERR_NOT_FOUND);
    TF_CHECK_INT(shell_exec_line("resume"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("resume NOPE"), -SHELL_ERR_NOT_FOUND);
    TF_CHECK_INT(shell_exec_line("unload"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("start"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("stop"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("fg"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("job"), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_line("waitjob"), -SHELL_ERR_PARAM);

    /* 无文件系统 */
    TF_CHECK_INT(shell_exec_line("load feed.ms"), -SHELL_ERR_NOT_FOUND);
    TF_CHECK_INT(shell_exec_line("run feed.ms"), -SHELL_ERR_NOT_FOUND);
    out_reset();
    TF_CHECK_INT(shell_exec_line("run"), 0);
    TF_CHECK(out_has("(empty)"));

    /* 危险命令 */
    TF_CHECK_INT(shell_exec_line("reboot"), -SHELL_ERR_DANGER);
    TF_CHECK_INT(shell_exec_line("reboot confirm"), -SHELL_ERR_PARAM);
    out_reset();
    TF_CHECK_INT(shell_exec_line("ver confirm"), 0);
    TF_CHECK(out_has("confirm ignored"));

    /* 错误输出格式 “ERR <code>:” */
    out_reset();
    TF_CHECK_INT(shell_exec_line("kill"), -SHELL_ERR_PARAM);
    TF_CHECK(out_has("ERR 1001:"));

    /* 未知命令 / 空行 / 注释 */
    TF_CHECK_INT(shell_exec_line("nosuchcmd"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line(""), 0);
    TF_CHECK_INT(shell_exec_line("   "), 0);
    TF_CHECK_INT(shell_exec_line("; comment"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line("// comment"), -SHELL_ERR_PARSE);
    TF_CHECK_INT(shell_exec_line(NULL), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_script(NULL), -SHELL_ERR_PARAM);
    TF_CHECK_INT(shell_exec_script(""), 0);
    TF_CHECK_INT(shell_exec_script("\n"), 0);

    tf_end();
}

static void test_ext_commands(void)
{
    int i;

    tf_begin("shell/扩展命令");

    shell_reset_all();

    TF_CHECK_INT(shell_register_cmd(NULL), -1);
    TF_CHECK_INT(shell_register_cmd(&s_ext_ok), 0);
    TF_CHECK_INT(shell_register_cmd(&s_ext_ok), -1);     /* 重名 */
    TF_CHECK_INT(shell_register_cmd(&s_ext_dup), -1);    /* 与内建冲突 */
    TF_CHECK_INT(shell_register_cmd(&s_ext_danger), 0);

    out_reset();
    TF_CHECK_INT(shell_exec_line("ext a b"), 0);
    TF_CHECK(out_has("ext argc=3"));

    out_reset();
    TF_CHECK_INT(shell_exec_line("help ext"), 0);
    TF_CHECK(out_has("ext [a]"));
    TF_CHECK(out_has("ext help"));

    /* 危险扩展命令 */
    TF_CHECK_INT(shell_exec_line("boom"), -SHELL_ERR_DANGER);
    TF_CHECK_INT(shell_exec_line("boom confirm"), -SHELL_ERR_DEVICE);

    /* 表满：最多 8 个扩展命令 */
    shell_reset_all();
    for (i = 0; i < 8; i++) {
        TF_CHECK_INT(shell_register_cmd(&s_ext_pool[i]), 0);
    }
    TF_CHECK_INT(shell_register_cmd(&s_ext_pool[8]), -1);

    tf_end();
}

static void test_input_buffer(void)
{
    static char big[300];

    tf_begin("shell/输入缓冲");

    shell_reset_all();

    TF_CHECK_INT(shell_feed(NULL, 0U), 0);
    TF_CHECK_INT(shell_feed("x", 0U), 0);

    /* 环形缓冲上限：容量 256，可用 255 */
    memset(big, 'a', sizeof(big));
    TF_CHECK_INT(shell_feed(big, (uint32_t)sizeof(big)), 255);
    TF_CHECK_INT(shell_feed("x", 1U), 0);

    shell_reset_all();
    TF_CHECK_INT(shell_feed("abc\n", 4U), 4);
    TF_CHECK_INT((int)shell_pending(), 4);
    TF_CHECK_INT(shell_feed("ok\n", 3U), 3);
    TF_CHECK_INT((int)shell_pending(), 7);

    /* 复位后输入缓冲清空 */
    shell_reset_all();
    TF_CHECK_INT((int)shell_pending(), 0);

    tf_end();
}

void ut_shell(void)
{
    shell_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.version = "test-build";
    cfg.write = capture_write;
    TF_CHECK_INT(shell_setup(&cfg), 0);

    test_init_and_lex();
    test_expr();
    test_units();
    test_vars();
    test_echo_print();
    test_control();
    test_on_error();
    test_jobs();
    test_cmd_errors();
    test_ext_commands();
    test_input_buffer();

    /* 末尾恢复干净状态，避免影响后续用例 */
    shell_reset_all();
    shell_reset();
}
