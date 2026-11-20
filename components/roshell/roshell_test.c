/*
 * roshell_test.c - roshell 组件单元测试（ut_roshell）
 *
 * 解释器依赖内核调度（run 任务 / wait time / load 后的任务回收），
 * 全部用例在 driver 任务内执行，模式与 src/ros_test.c 一致。
 */

#include "test.h"
#include "ros.h"
#include "roshell.h"

#include <stdio.h>
#include <string.h>

static ros_task_t s_roshell_driver;
static uint8_t s_roshell_stack[8192];
static roshell_t *g_rs;

/* ---------- 辅助 ---------- */

static int rsexec(const char *script)
{
    roshell_err_t e;

    memset(&e, 0, sizeof(e));
    return roshell_exec(g_rs, script, &e);
}

static int rsexec_e(const char *script, roshell_err_t *e)
{
    memset(e, 0, sizeof(*e));
    return roshell_exec(g_rs, script, e);
}

static int rsline(const char *line)
{
    roshell_err_t e;

    memset(&e, 0, sizeof(e));
    return roshell_exec_line(g_rs, line, &e);
}

static int32_t rsi(const char *name)
{
    int32_t v = 0x7fffffff;

    roshell_get_int(g_rs, name, &v);
    return v;
}

static float rsf(const char *name)
{
    float v = 0.0f;

    roshell_get_float(g_rs, name, &v);
    return v;
}

static bool rsb(const char *name)
{
    bool v = false;

    roshell_get_bool(g_rs, name, &v);
    return v;
}

/* ---------- 用例分组 ---------- */

static void ro_t_lifecycle(void)
{
    roshell_t *rs;
    roshell_err_t e;
    int32_t v;

    tf_begin("roshell:lifecycle");
    rs = roshell_create();
    TF_CHECK(rs != NULL);
    roshell_destroy(rs);
    roshell_destroy(NULL);
    TF_CHECK_INT(roshell_exec(NULL, "$x=1", &e), RO_ERR_PARAM);
    TF_CHECK_INT(roshell_exec(g_rs, NULL, &e), RO_ERR_PARAM);
    TF_CHECK_INT(roshell_get_int(NULL, "x", &v), RO_ERR_PARAM);
    TF_CHECK_INT(roshell_get_int(g_rs, "never", &v), RO_ERR_UNDEF_VAR);
    TF_CHECK_INT(rsexec(NULL), RO_ERR_PARAM);
    TF_CHECK_INT(rsexec(""), RO_OK);
    TF_CHECK_INT(rsexec("; 纯注释\n\n  \n// 另一条\n"), RO_OK);
    tf_end();
}

static void ro_t_arith(void)
{
    tf_begin("roshell:arith");
    TF_CHECK_INT(rsexec("$x = 2 + 3 * 4"), RO_OK);
    TF_CHECK_INT(rsi("x"), 14);
    TF_CHECK_INT(rsexec("$x = (2 + 3) * 4"), RO_OK);
    TF_CHECK_INT(rsi("$x"), 20);
    TF_CHECK_INT(rsexec("$x = -5 + 2"), RO_OK);
    TF_CHECK_INT(rsi("x"), -3);
    TF_CHECK_INT(rsexec("$x = 10 / 3"), RO_OK);
    TF_CHECK_INT(rsi("x"), 3);
    TF_CHECK_INT(rsexec("$x = 10 % 3"), RO_OK);
    TF_CHECK_INT(rsi("x"), 1);
    TF_CHECK_INT(rsexec("$x = 10 % 0"), RO_ERR_DIV_ZERO);
    TF_CHECK_INT(rsexec("$x = 2.5 + 2.5"), RO_OK);
    TF_CHECK_NEAR(rsf("x"), 5.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = 1 + 2.5"), RO_OK);
    TF_CHECK_NEAR(rsf("x"), 3.5, 1e-6);
    TF_CHECK_INT(rsexec("$x = 2e3"), RO_OK);
    TF_CHECK_NEAR(rsf("x"), 2000.0, 1e-3);
    TF_CHECK_INT(rsexec("$x = 1.5e-2"), RO_OK);
    TF_CHECK_NEAR(rsf("x"), 0.015, 1e-6);
    TF_CHECK_INT(rsexec("$x = 1 / 0"), RO_OK);
    TF_CHECK(rsf("x") > 1e30f);
    TF_CHECK_INT(rsexec("$x = -1 / 0"), RO_OK);
    TF_CHECK(rsf("x") < -1e30f);
    TF_CHECK_INT(rsexec("$x = 10.0 / 0"), RO_OK);
    TF_CHECK(rsf("x") > 1e30f);
    TF_CHECK_INT(rsexec("$x = 3.0 == 3"), RO_OK);
    TF_CHECK(rsb("x"));
    TF_CHECK_INT(rsexec("$x = 7 / 2.0"), RO_OK);
    TF_CHECK_NEAR(rsf("x"), 3.5, 1e-6);
    TF_CHECK_INT(rsexec("$x = 2 * 3 + 4 * 5"), RO_OK);
    TF_CHECK_INT(rsi("x"), 26);
    TF_CHECK_INT(rsexec("$x = \"a\" - 1"), RO_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = true + 1"), RO_ERR_TYPE);
    tf_end();
}

static void ro_t_logic(void)
{
    tf_begin("roshell:logic");
    TF_CHECK_INT(rsexec("$b = 1 < 2"), RO_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = 2 <= 2"), RO_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = 3 > 4"), RO_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = 4 >= 4"), RO_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = !true"), RO_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = true && false"), RO_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = true || false"), RO_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = 1 != 2"), RO_OK);
    TF_CHECK(rsb("b"));
    /* 短路：被跳过的一侧含运行期错误也不会触发 */
    TF_CHECK_INT(rsexec("$b = (1 < 2) || (10 % 0 == 0)"), RO_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = (2 < 1) && (10 % 0 == 0)"), RO_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = (1 < 2) && (10 % 0 == 0)"), RO_ERR_DIV_ZERO);
    TF_CHECK_INT(rsexec("$b = !5"), RO_ERR_TYPE);
    TF_CHECK_INT(rsexec("$b = 1 && true"), RO_ERR_TYPE);
    TF_CHECK_INT(rsexec("$b = true == 1"), RO_ERR_TYPE);
    TF_CHECK_INT(rsexec("$b = \"a\" == 1"), RO_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = \"a\" != 1"), RO_OK);
    TF_CHECK(rsb("b"));
    tf_end();
}

static void ro_t_string(void)
{
    char buf[64];

    tf_begin("roshell:string");
    TF_CHECK_INT(rsexec("$s = \"ab\" + \"c\""), RO_OK);
    TF_CHECK_INT(roshell_get_str(g_rs, "s", buf, sizeof(buf)), RO_OK);
    TF_CHECK_STR(buf, "abc");
    TF_CHECK_INT(rsexec("$s = \"a\\\"b\""), RO_OK);
    TF_CHECK_INT(roshell_get_str(g_rs, "s", buf, sizeof(buf)), RO_OK);
    TF_CHECK_STR(buf, "a\"b");
    /* 规格 §3.1：仅 \" 为转义，\\ 保持原样 */
    TF_CHECK_INT(rsexec("$s = \"a\\\\b\""), RO_OK);
    TF_CHECK_INT(roshell_get_str(g_rs, "s", buf, sizeof(buf)), RO_OK);
    TF_CHECK_STR(buf, "a\\\\b");
    TF_CHECK_INT(rsexec("$s = \"n=\" + 5"), RO_OK);
    TF_CHECK_INT(roshell_get_str(g_rs, "s", buf, sizeof(buf)), RO_OK);
    TF_CHECK_STR(buf, "n=5");
    TF_CHECK_INT(rsexec("$s = \"v=\" + true"), RO_OK);
    TF_CHECK_INT(roshell_get_str(g_rs, "s", buf, sizeof(buf)), RO_OK);
    TF_CHECK_STR(buf, "v=true");
    TF_CHECK_INT(rsexec("$b = \"a\" == \"a\""), RO_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = \"a\" == \"b\""), RO_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$s = \"48chars48chars48chars48chars48chars48chars48charsAB\""),
                 RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("$s = \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\" + "
                        "\"bbbbbbbbbb\""), RO_ERR_LIMIT);
    TF_CHECK_INT(rsexec("$s = \"abc"), RO_ERR_PARSE);
    /* 截断安全读取 */
    TF_CHECK_INT(rsexec("$s = \"abcdef\""), RO_OK);
    TF_CHECK_INT(roshell_get_str(g_rs, "s", buf, 3), RO_OK);
    TF_CHECK_STR(buf, "ab");
    TF_CHECK_INT(roshell_get_str(g_rs, "s", buf, 0), RO_ERR_PARAM);
    tf_end();
}

static void ro_t_vars(void)
{
    char buf[64];

    tf_begin("roshell:variables");
    TF_CHECK_INT(rsexec("$x = 5\n$x++"), RO_OK);
    TF_CHECK_INT(rsi("x"), 6);
    /* 规格 §3.2：后缀先取值后加减，前缀先加减后取值 */
    TF_CHECK_INT(rsexec("$y = $x++"), RO_OK);
    TF_CHECK_INT(rsi("y"), 6);
    TF_CHECK_INT(rsi("x"), 7);
    TF_CHECK_INT(rsexec("$y = --$x"), RO_OK);
    TF_CHECK_INT(rsi("y"), 6);
    TF_CHECK_INT(rsexec("$x--"), RO_OK);
    TF_CHECK_INT(rsi("x"), 5);
    TF_CHECK_INT(rsexec("$f = 1.5\n$f++"), RO_OK);
    TF_CHECK_NEAR(rsf("f"), 2.5, 1e-6);
    TF_CHECK_INT(rsexec("$x = \"s\""), RO_OK);
    TF_CHECK_INT(roshell_get_str(g_rs, "x", buf, sizeof(buf)), RO_OK);
    TF_CHECK_STR(buf, "s");
    TF_CHECK_INT(roshell_get_int(g_rs, "x", NULL), RO_ERR_PARAM);
    TF_CHECK_INT(rsexec("$x = true"), RO_OK);
    TF_CHECK(rsb("x"));
    TF_CHECK_INT(rsexec("$b = $nope"), RO_ERR_UNDEF_VAR);
    TF_CHECK_INT(rsexec("$z++"), RO_ERR_UNDEF_VAR);
    /* 类型严格 getter */
    TF_CHECK_INT(rsexec("$n = 5"), RO_OK);
    {
        float fv;
        TF_CHECK_INT(roshell_get_float(g_rs, "n", &fv), RO_ERR_TYPE);
    }
    tf_end();
}

static void ro_t_if(void)
{
    tf_begin("roshell:if");
    TF_CHECK_INT(rsexec("$x = 5\nif $x > 7\n$y = 1\nelseif $x > 3\n$y = 2\n"
                        "else\n$y = 3\nend"), RO_OK);
    TF_CHECK_INT(rsi("y"), 2);
    TF_CHECK_INT(rsexec("if true\n$y = 10\nelse\n$y = 20\nend"), RO_OK);
    TF_CHECK_INT(rsi("y"), 10);
    TF_CHECK_INT(rsexec("if false\n$y = 10\nelse\n$y = 20\nend"), RO_OK);
    TF_CHECK_INT(rsi("y"), 20);
    /* 假分支整体跳过（含嵌套块） */
    TF_CHECK_INT(rsexec("$x = 0\nif false\nif true\n$x = 1\nend\n$x = 2\nend"), RO_OK);
    TF_CHECK_INT(rsi("x"), 0);
    /* 嵌套块后的 elseif */
    TF_CHECK_INT(rsexec("$z = 0\nif false\nif true\n$z = 1\nend\nelseif true\n"
                        "$z = 2\nend"), RO_OK);
    TF_CHECK_INT(rsi("z"), 2);
    TF_CHECK_INT(rsexec("if 5\nend"), RO_ERR_TYPE);
    TF_CHECK_INT(rsexec("if $nope\nend"), RO_ERR_UNDEF_VAR);
    tf_end();
}

static void ro_t_for(void)
{
    tf_begin("roshell:for");
    TF_CHECK_INT(rsexec("$t = 0\nfor $i = 1,2,3 do\n$t = $t + $i\nend"), RO_OK);
    TF_CHECK_INT(rsi("t"), 6);
    TF_CHECK_INT(rsi("i"), 3);
    /* 表达式项 */
    TF_CHECK_INT(rsexec("$n = 0\nfor 1 + 1, 2 * 2 do\n$n = $n + 1\nend"), RO_OK);
    TF_CHECK_INT(rsi("n"), 2);
    /* 无绑定变量 */
    TF_CHECK_INT(rsexec("$c = 0\nfor \"a\", \"b\" do\n$c = $c + 1\nend"), RO_OK);
    TF_CHECK_INT(rsi("c"), 2);
    /* 惰性求值：第 2 项未定义 */
    TF_CHECK_INT(rsexec("$c = 0\nfor 1, $nope do\n$c = $c + 1\nend"), RO_ERR_UNDEF_VAR);
    TF_CHECK_INT(rsi("c"), 1);
    /* 嵌套 + break */
    TF_CHECK_INT(rsexec("$s = 0\nfor $a = 1,2 do\nfor $b = 1,2,3 do\n"
                        "if $b == 3\nbreak\nend\n$s = $s + 1\nend\nend"), RO_OK);
    TF_CHECK_INT(rsi("s"), 4);
    /* 循环变量参与表达式 */
    TF_CHECK_INT(rsexec("$p = 1\nfor $k = 1,2,3,4 do\n$p = $p * $k\nend"), RO_OK);
    TF_CHECK_INT(rsi("p"), 24);
    TF_CHECK_INT(rsexec("for 1,2\nend"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("for do\nend"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("for 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 do\nend"), RO_ERR_LIMIT);
    TF_CHECK_INT(rsexec("for $bad = 1 do\nend\n"), RO_OK);
    tf_end();
}

static void ro_t_while(void)
{
    tf_begin("roshell:while");
    TF_CHECK_INT(rsexec("$i = 0\nwhile true\n$i = $i + 1\nif $i > 2\nbreak\nend\nend"),
                 RO_OK);
    TF_CHECK_INT(rsi("i"), 3);
    TF_CHECK_INT(rsexec("$i = 3\nwhile $i > 0\n$i = $i - 1\nend"), RO_OK);
    TF_CHECK_INT(rsi("i"), 0);
    TF_CHECK_INT(rsexec("$y = 0\nwhile false\n$y = 1\nend"), RO_OK);
    TF_CHECK_INT(rsi("y"), 0);
    /* while 内嵌套 if 的 end 不与 while 混淆 */
    TF_CHECK_INT(rsexec("$i = 0\nwhile $i < 3\nif true\n$i = $i + 1\nend\nend"), RO_OK);
    TF_CHECK_INT(rsi("i"), 3);
    tf_end();
}

static void ro_t_func(void)
{
    char buf[64];

    tf_begin("roshell:function");
    TF_CHECK_INT(rsexec("function add($a, $b)\nreturn $a + $b\nend\n"
                        "$x = add(2, 3) + 1"), RO_OK);
    TF_CHECK_INT(rsi("x"), 6);
    /* 函数作用域为脚本级：定义与调用须在同一脚本内 */
    TF_CHECK_INT(rsexec("function add($a, $b)\nreturn $a + $b\nend\ncall add(1, 2)"),
                 RO_OK);
    /* 前向引用 */
    TF_CHECK_INT(rsexec("$x = late()\nfunction late()\nreturn 9\nend"), RO_OK);
    TF_CHECK_INT(rsi("x"), 9);
    /* 字符串返回 */
    TF_CHECK_INT(rsexec("function greet($n)\nreturn \"hi \" + $n\nend\n"
                        "$g = greet(\"bob\")"), RO_OK);
    TF_CHECK_INT(roshell_get_str(g_rs, "g", buf, sizeof(buf)), RO_OK);
    TF_CHECK_STR(buf, "hi bob");
    /* 函数变量私有 */
    TF_CHECK_INT(rsexec("function setx()\n$x = 99\nend\n$x = 1\ncall setx()"), RO_OK);
    TF_CHECK_INT(rsi("x"), 1);
    /* 递归深度：CALL_DEPTH=4，rec(3) 可达，rec(4) 溢出 */
    TF_CHECK_INT(rsexec("function rec($n)\nif $n > 0\nreturn rec($n - 1)\nend\n"
                        "return 0\nend\n$x = rec(3)"), RO_OK);
    TF_CHECK_INT(rsexec("function rec($n)\nif $n > 0\nreturn rec($n - 1)\nend\n"
                        "return 0\nend\n$x = rec(4)"), RO_ERR_STACK_OVERFLOW);
    /* 错误 */
    TF_CHECK_INT(rsexec("function add($a, $b)\nreturn $a + $b\nend\nadd(1)"),
                 RO_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("function add($a, $b)\nreturn $a + $b\nend\ncall add(1, 2, 3)"),
                 RO_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("nope()"), RO_ERR_UNDEF_FUNC);
    TF_CHECK_INT(rsexec("call nope()"), RO_ERR_UNDEF_FUNC);
    TF_CHECK_INT(rsexec("function dup()\nend\nfunction dup()\nend"), RO_ERR_DUPLICATE);
    TF_CHECK_INT(rsexec("function nf()\nend\n$x = nf()"), RO_ERR_TYPE);
    TF_CHECK_INT(rsexec("function zero()\nend\nzero(1)"), RO_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("function f8($a,$b,$c,$d,$e,$f,$g,$h)\nreturn $a\nend"), RO_OK);
    TF_CHECK_INT(rsexec("function f9($a,$b,$c,$d,$e,$f,$g,$h,$i)\nend"), RO_ERR_LIMIT);
    TF_CHECK_INT(rsexec("function bad($x\nend"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("return 1"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("function ok()\nend\ncall ok()"), RO_OK);
    tf_end();
}

static void ro_t_errors(void)
{
    roshell_err_t e;
    char big[ROSHELL_SCRIPT_MAX + 100U];
    char line[ROSHELL_LINE_MAX + 100U];
    char buf[4096];
    uint32_t i;
    uint32_t n = 0U;

    tf_begin("roshell:errors");
    TF_CHECK_INT(rsexec("if true"), RO_ERR_BLOCK);
    TF_CHECK_INT(rsexec("end"), RO_ERR_BLOCK);
    TF_CHECK_INT(rsexec("elseif true\nend"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("if true\nelse\nelse\nend"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("if true\nelse\nelseif false\nend"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("break"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("load 5"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("wait time"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("= 5"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("$x = 1 +"), RO_ERR_PARSE);
    TF_CHECK_INT(rsexec("IF true\nend"), RO_ERR_BLOCK);
    /* 行长 / 脚本长 / 函数数 */
    for (i = 0U; i < ROSHELL_LINE_MAX + 50U; i++) {
        line[i] = 'a';
    }
    line[i] = '\0';
    TF_CHECK_INT(rsexec(line), RO_ERR_LIMIT);
    while (n + 6U < sizeof(big)) {
        memcpy(big + n, "$x=1\n;", 6U);
        n += 6U;
    }
    big[n] = '\0';
    TF_CHECK_INT(rsexec(big), RO_ERR_LIMIT);
    buf[0] = '\0';
    for (i = 0U; i < 33U; i++) {
        char fn[32];

        snprintf(fn, sizeof(fn), "function f%u()\nend\n", (unsigned)i);
        if (strlen(buf) + strlen(fn) < sizeof(buf)) {
            strcat(buf, fn);
        }
    }
    TF_CHECK_INT(rsexec(buf), RO_ERR_LIMIT);
    /* 错误行号 */
    TF_CHECK_INT(rsexec_e("$x = 1\n$y = 2\n$z = $nope\n", &e), RO_ERR_UNDEF_VAR);
    TF_CHECK_INT(e.line, 3);
    TF_CHECK_INT(rsexec_e("if true\n", &e), RO_ERR_BLOCK);
    tf_end();
}

static void ro_t_run(void)
{
    roshell_err_t e;
    int slot;
    int i;
    int slots[ROSHELL_RUN_MAX];

    tf_begin("roshell:run");
    memset(&e, 0, sizeof(e));
    slot = roshell_run_async(g_rs, "$x = 1", &e);
    TF_CHECK(slot >= 0);
    TF_CHECK_INT(roshell_run_state(g_rs, slot), RO_RUN_RUNNING);
    TF_CHECK_INT(roshell_run_wait(g_rs, slot, ROS_TIMEOUT_FOREVER), RO_OK);
    /* 任务内错误 */
    slot = roshell_run_async(g_rs, "$y = $nope", &e);
    TF_CHECK(slot >= 0);
    TF_CHECK_INT(roshell_run_wait(g_rs, slot, ROS_TIMEOUT_FOREVER), RO_ERR_UNDEF_VAR);
    /* 超时后继续等待 */
    slot = roshell_run_async(g_rs, "wait time 200\n$x = 2", &e);
    TF_CHECK(slot >= 0);
    TF_CHECK_INT(roshell_run_wait(g_rs, slot, 20), RO_ERR_TIMEOUT);
    TF_CHECK_INT(roshell_run_wait(g_rs, slot, ROS_TIMEOUT_FOREVER), RO_OK);
    /* 扫描错误直接返回 */
    TF_CHECK_INT(roshell_run_async(g_rs, "if true", &e), RO_ERR_BLOCK);
    /* run 语句（脚本内） */
    TF_CHECK_INT(rsexec("function bg()\nend\nrun bg()"), RO_OK);
    TF_CHECK_INT(rsexec("function bg2($n)\nend\nrun bg2(3)"), RO_OK);
    TF_CHECK_INT(rsexec("run nofunc()"), RO_ERR_UNDEF_FUNC);
    /* 槽位参数 */
    TF_CHECK_INT(roshell_run_wait(g_rs, -1, 0), RO_ERR_PARAM);
    TF_CHECK_INT(roshell_run_state(g_rs, (int)ROSHELL_RUN_MAX), RO_ERR_PARAM);
    /* 槽位表满：先让出 CPU，使前面 run 语句派生的任务结束并可回收 */
    ros_sleep_ms(50);
    for (i = 0; i < (int)ROSHELL_RUN_MAX; i++) {
        slots[i] = roshell_run_async(g_rs, "wait time 200", &e);
        if (slots[i] < 0) {
            break;
        }
    }
    TF_CHECK_INT(i, (int)ROSHELL_RUN_MAX);
    TF_CHECK_INT(roshell_run_async(g_rs, "$x = 1", &e), RO_ERR_LIMIT);
    for (i = 0; i < (int)ROSHELL_RUN_MAX; i++) {
        TF_CHECK_INT(roshell_run_wait(g_rs, slots[i], ROS_TIMEOUT_FOREVER), RO_OK);
    }
    tf_end();
}

static void ro_t_load(void)
{
    roshell_err_t e;
    FILE *fp;

    tf_begin("roshell:load");
    fp = fopen("roshell_ut.tmp", "w");
    TF_CHECK(fp != NULL);
    if (fp != NULL) {
        fputs("$v = 40 + 2\n; 注释\n", fp);
        fclose(fp);
        memset(&e, 0, sizeof(e));
        TF_CHECK_INT(roshell_load(g_rs, "roshell_ut.tmp", &e), RO_OK);
        TF_CHECK_INT(rsi("v"), 42);
        /* 脚本内 load 语句 */
        TF_CHECK_INT(rsexec("load \"roshell_ut.tmp\""), RO_OK);
        /* 文件内解析错误：行号按文件计 */
        fclose(fopen("roshell_ut.tmp", "w"));
        fp = fopen("roshell_ut.tmp", "w");
        fputs("$a = 1\nif true\n", fp);
        fclose(fp);
        memset(&e, 0, sizeof(e));
        TF_CHECK_INT(roshell_load(g_rs, "roshell_ut.tmp", &e), RO_ERR_BLOCK);
        TF_CHECK_INT(e.line, 2);
        remove("roshell_ut.tmp");
    }
    memset(&e, 0, sizeof(e));
    TF_CHECK_INT(roshell_load(g_rs, "roshell_missing.tmp", &e), RO_ERR_NOT_FOUND);
    TF_CHECK_INT(rsexec("load \"roshell_missing.tmp\""), RO_ERR_NOT_FOUND);
    tf_end();
}

static void ro_t_exec_line(void)
{
    tf_begin("roshell:exec_line");
    TF_CHECK_INT(rsline("$a = 1"), RO_OK);
    TF_CHECK_INT(rsline("$a = $a + 1"), RO_OK);
    TF_CHECK_INT(rsi("a"), 2);
    TF_CHECK_INT(rsline("$a++"), RO_OK);
    TF_CHECK_INT(rsi("a"), 3);
    TF_CHECK_INT(rsline("if true"), RO_ERR_PARSE);
    TF_CHECK_INT(rsline("end"), RO_ERR_PARSE);
    TF_CHECK_INT(rsline("function f()"), RO_ERR_PARSE);
    TF_CHECK_INT(rsline("for $i = 1 do"), RO_ERR_PARSE);
    TF_CHECK_INT(rsline("while true"), RO_ERR_PARSE);
    TF_CHECK_INT(rsline("wait time 1"), RO_OK);
    TF_CHECK_INT(rsline("call nope()"), RO_ERR_UNDEF_FUNC);
    TF_CHECK_INT(rsline("$a = $a + 1"), RO_OK);
    TF_CHECK_INT(rsi("a"), 4);
    tf_end();
}

static void ro_t_wait(void)
{
    tf_begin("roshell:wait");
    TF_CHECK_INT(rsexec("wait time 1"), RO_OK);
    TF_CHECK_INT(rsexec("wait time 5.7"), RO_OK);
    TF_CHECK_INT(rsexec("wait time 1 + 1"), RO_OK);
    TF_CHECK_INT(rsexec("wait time -5"), RO_OK);
    TF_CHECK_INT(rsexec("wait time \"s\""), RO_ERR_TYPE);
    TF_CHECK_INT(rsexec("wait 5"), RO_ERR_PARSE);
    tf_end();
}

static void roshell_driver_entry(void *arg)
{
    (void)arg;

    g_rs = roshell_create();
    TF_CHECK(g_rs != NULL);
    if (g_rs != NULL) {
        ro_t_lifecycle();
        ro_t_arith();
        ro_t_logic();
        ro_t_string();
        ro_t_vars();
        ro_t_if();
        ro_t_for();
        ro_t_while();
        ro_t_func();
        ro_t_errors();
        ro_t_run();
        ro_t_load();
        ro_t_exec_line();
        ro_t_wait();
        ros_sleep_ms(100);       /* 让 run 语句派生的任务全部结束 */
        roshell_destroy(g_rs);
        g_rs = NULL;
    }
    ros_stop();
}

void ut_roshell(void)
{
    ros_init();

    TF_CHECK_INT(ros_task_create(&s_roshell_driver, "rosh_ut", 0,
                                 roshell_driver_entry, NULL,
                                 s_roshell_stack, sizeof(s_roshell_stack)),
                 ROS_OK);

    ros_sched_start();
}
