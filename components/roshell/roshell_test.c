/*
 * roshell_test.c - roshell 组件单元测试（ut_roshell）
 *
 * 解释器依赖内核调度（run 任务 / wait 命令后的任务回收），
 * 全部用例在 driver 任务内执行，模式与 src/ros_test.c 一致。
 */

#include "test.h"
#include "ros.h"
#include "roshell.h"
#include "roshell.h"

#include <stdio.h>
#include <string.h>

static ros_task_t s_roshell_driver;
static uint8_t s_roshell_stack[8192];
static rs_t *g_rs;

/* ---------- 辅助 ---------- */

static int rsexec(const char *script)
{
    rs_err_t e;

    memset(&e, 0, sizeof(e));
    return rs_exec(g_rs, script, &e);
}

static int rsexec_e(const char *script, rs_err_t *e)
{
    memset(e, 0, sizeof(*e));
    return rs_exec(g_rs, script, e);
}

static int rsline(const char *line)
{
    rs_err_t e;

    memset(&e, 0, sizeof(e));
    return rs_exec_line(g_rs, line, &e);
}

static int32_t rsi(const char *name)
{
    int32_t v = 0x7fffffff;

    rs_get_int(g_rs, name, &v);
    return v;
}

static float rsf(const char *name)
{
    float v = 0.0f;

    rs_get_float(g_rs, name, &v);
    return v;
}

static bool rsb(const char *name)
{
    bool v = false;

    rs_get_bool(g_rs, name, &v);
    return v;
}

/* ---------- 用例分组 ---------- */

static void rs_t_lifecycle(void)
{
    rs_t *rs;
    rs_err_t e;
    int32_t v;

    tf_begin("roshell:lifecycle");
    rs = rs_create();
    TF_CHECK(rs != NULL);
    rs_destroy(rs);
    rs_destroy(NULL);
    TF_CHECK_INT(rs_exec(NULL, "$x=1", &e), RS_ERR_PARAM);
    TF_CHECK_INT(rs_exec(g_rs, NULL, &e), RS_ERR_PARAM);
    TF_CHECK_INT(rs_get_int(NULL, "x", &v), RS_ERR_PARAM);
    TF_CHECK_INT(rs_get_int(g_rs, "never", &v), RS_ERR_UNDEF_VAR);
    TF_CHECK_INT(rsexec(NULL), RS_ERR_PARAM);
    TF_CHECK_INT(rsexec(""), RS_OK);
    TF_CHECK_INT(rsexec("; 纯注释\n\n  \n// 另一条\n"), RS_OK);
    tf_end();
}

static void rs_t_arith(void)
{
    tf_begin("roshell:arith");
    TF_CHECK_INT(rsexec("$x = 2 + 3 * 4"), RS_OK);
    TF_CHECK_INT(rsi("x"), 14);
    TF_CHECK_INT(rsexec("$x = (2 + 3) * 4"), RS_OK);
    TF_CHECK_INT(rsi("$x"), 20);
    TF_CHECK_INT(rsexec("$x = -5 + 2"), RS_OK);
    TF_CHECK_INT(rsi("x"), -3);
    TF_CHECK_INT(rsexec("$x = 10 / 3"), RS_OK);
    TF_CHECK_INT(rsi("x"), 3);
    TF_CHECK_INT(rsexec("$x = 10 % 3"), RS_OK);
    TF_CHECK_INT(rsi("x"), 1);
    TF_CHECK_INT(rsexec("$x = 10 % 0"), RS_ERR_DIV_ZERO);
    TF_CHECK_INT(rsexec("$x = 2.5 + 2.5"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 5.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = 1 + 2.5"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.5, 1e-6);
    TF_CHECK_INT(rsexec("$x = 2e3"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 2000.0, 1e-3);
    TF_CHECK_INT(rsexec("$x = 1.5e-2"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.015, 1e-6);
    TF_CHECK_INT(rsexec("$x = 1 / 0"), RS_OK);
    TF_CHECK(rsf("x") > 1e30f);
    TF_CHECK_INT(rsexec("$x = -1 / 0"), RS_OK);
    TF_CHECK(rsf("x") < -1e30f);
    TF_CHECK_INT(rsexec("$x = 10.0 / 0"), RS_OK);
    TF_CHECK(rsf("x") > 1e30f);
    TF_CHECK_INT(rsexec("$x = 3.0 == 3"), RS_OK);
    TF_CHECK(rsb("x"));
    TF_CHECK_INT(rsexec("$x = 7 / 2.0"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.5, 1e-6);
    TF_CHECK_INT(rsexec("$x = 2 * 3 + 4 * 5"), RS_OK);
    TF_CHECK_INT(rsi("x"), 26);
    TF_CHECK_INT(rsexec("$x = \"a\" - 1"), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = true + 1"), RS_ERR_TYPE);
    tf_end();
}

static void rs_t_logic(void)
{
    tf_begin("roshell:logic");
    TF_CHECK_INT(rsexec("$b = 1 < 2"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = 2 <= 2"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = 3 > 4"), RS_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = 4 >= 4"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = !true"), RS_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = true && false"), RS_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = true || false"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = 1 != 2"), RS_OK);
    TF_CHECK(rsb("b"));
    /* 短路：被跳过的一侧含运行期错误也不会触发 */
    TF_CHECK_INT(rsexec("$b = (1 < 2) || (10 % 0 == 0)"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = (2 < 1) && (10 % 0 == 0)"), RS_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = (1 < 2) && (10 % 0 == 0)"), RS_ERR_DIV_ZERO);
    TF_CHECK_INT(rsexec("$b = !5"), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$b = 1 && true"), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$b = true == 1"), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$b = \"a\" == 1"), RS_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = \"a\" != 1"), RS_OK);
    TF_CHECK(rsb("b"));
    tf_end();
}

static void rs_t_string(void)
{
    char buf[64];

    tf_begin("roshell:string");
    TF_CHECK_INT(rsexec("$s = \"ab\" + \"c\""), RS_OK);
    TF_CHECK_INT(rs_get_str(g_rs, "s", buf, sizeof(buf)), RS_OK);
    TF_CHECK_STR(buf, "abc");
    TF_CHECK_INT(rsexec("$s = \"a\\\"b\""), RS_OK);
    TF_CHECK_INT(rs_get_str(g_rs, "s", buf, sizeof(buf)), RS_OK);
    TF_CHECK_STR(buf, "a\"b");
    /* 规格 §3.1：仅 \" 为转义，\\ 保持原样 */
    TF_CHECK_INT(rsexec("$s = \"a\\\\b\""), RS_OK);
    TF_CHECK_INT(rs_get_str(g_rs, "s", buf, sizeof(buf)), RS_OK);
    TF_CHECK_STR(buf, "a\\\\b");
    TF_CHECK_INT(rsexec("$s = \"n=\" + 5"), RS_OK);
    TF_CHECK_INT(rs_get_str(g_rs, "s", buf, sizeof(buf)), RS_OK);
    TF_CHECK_STR(buf, "n=5");
    TF_CHECK_INT(rsexec("$s = \"v=\" + true"), RS_OK);
    TF_CHECK_INT(rs_get_str(g_rs, "s", buf, sizeof(buf)), RS_OK);
    TF_CHECK_STR(buf, "v=true");
    TF_CHECK_INT(rsexec("$b = \"a\" == \"a\""), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = \"a\" == \"b\""), RS_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$s = \"48chars48chars48chars48chars48chars48chars48charsAB\""),
                 RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("$s = \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\" + "
                        "\"bbbbbbbbbb\""), RS_ERR_LIMIT);
    TF_CHECK_INT(rsexec("$s = \"abc"), RS_ERR_PARSE);
    /* 截断安全读取 */
    TF_CHECK_INT(rsexec("$s = \"abcdef\""), RS_OK);
    TF_CHECK_INT(rs_get_str(g_rs, "s", buf, 3), RS_OK);
    TF_CHECK_STR(buf, "ab");
    TF_CHECK_INT(rs_get_str(g_rs, "s", buf, 0), RS_ERR_PARAM);
    tf_end();
}

static void rs_t_vars(void)
{
    char buf[64];

    tf_begin("roshell:variables");
    TF_CHECK_INT(rsexec("$x = 5\n$x++"), RS_OK);
    TF_CHECK_INT(rsi("x"), 6);
    /* 规格 §3.2：后缀先取值后加减，前缀先加减后取值 */
    TF_CHECK_INT(rsexec("$y = $x++"), RS_OK);
    TF_CHECK_INT(rsi("y"), 6);
    TF_CHECK_INT(rsi("x"), 7);
    TF_CHECK_INT(rsexec("$y = --$x"), RS_OK);
    TF_CHECK_INT(rsi("y"), 6);
    TF_CHECK_INT(rsexec("$x--"), RS_OK);
    TF_CHECK_INT(rsi("x"), 5);
    TF_CHECK_INT(rsexec("$f = 1.5\n$f++"), RS_OK);
    TF_CHECK_NEAR(rsf("f"), 2.5, 1e-6);
    TF_CHECK_INT(rsexec("$x = \"s\""), RS_OK);
    TF_CHECK_INT(rs_get_str(g_rs, "x", buf, sizeof(buf)), RS_OK);
    TF_CHECK_STR(buf, "s");
    TF_CHECK_INT(rs_get_int(g_rs, "x", NULL), RS_ERR_PARAM);
    TF_CHECK_INT(rsexec("$x = true"), RS_OK);
    TF_CHECK(rsb("x"));
    TF_CHECK_INT(rsexec("$b = $nope"), RS_ERR_UNDEF_VAR);
    TF_CHECK_INT(rsexec("$z++"), RS_ERR_UNDEF_VAR);
    /* 类型严格 getter */
    TF_CHECK_INT(rsexec("$n = 5"), RS_OK);
    {
        float fv;
        TF_CHECK_INT(rs_get_float(g_rs, "n", &fv), RS_ERR_TYPE);
    }
    tf_end();
}

static void rs_t_if(void)
{
    tf_begin("roshell:if");
    TF_CHECK_INT(rsexec("$x = 5\nif $x > 7\n$y = 1\nelseif $x > 3\n$y = 2\n"
                        "else\n$y = 3\nend"), RS_OK);
    TF_CHECK_INT(rsi("y"), 2);
    TF_CHECK_INT(rsexec("if true\n$y = 10\nelse\n$y = 20\nend"), RS_OK);
    TF_CHECK_INT(rsi("y"), 10);
    TF_CHECK_INT(rsexec("if false\n$y = 10\nelse\n$y = 20\nend"), RS_OK);
    TF_CHECK_INT(rsi("y"), 20);
    /* 假分支整体跳过（含嵌套块） */
    TF_CHECK_INT(rsexec("$x = 0\nif false\nif true\n$x = 1\nend\n$x = 2\nend"), RS_OK);
    TF_CHECK_INT(rsi("x"), 0);
    /* 嵌套块后的 elseif */
    TF_CHECK_INT(rsexec("$z = 0\nif false\nif true\n$z = 1\nend\nelseif true\n"
                        "$z = 2\nend"), RS_OK);
    TF_CHECK_INT(rsi("z"), 2);
    TF_CHECK_INT(rsexec("if 5\nend"), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("if $nope\nend"), RS_ERR_UNDEF_VAR);
    tf_end();
}

static void rs_t_for(void)
{
    tf_begin("roshell:for");
    TF_CHECK_INT(rsexec("$t = 0\nfor $i = 1,2,3 do\n$t = $t + $i\nend"), RS_OK);
    TF_CHECK_INT(rsi("t"), 6);
    TF_CHECK_INT(rsi("i"), 3);
    /* 表达式项 */
    TF_CHECK_INT(rsexec("$n = 0\nfor 1 + 1, 2 * 2 do\n$n = $n + 1\nend"), RS_OK);
    TF_CHECK_INT(rsi("n"), 2);
    /* 无绑定变量 */
    TF_CHECK_INT(rsexec("$c = 0\nfor \"a\", \"b\" do\n$c = $c + 1\nend"), RS_OK);
    TF_CHECK_INT(rsi("c"), 2);
    /* 惰性求值：第 2 项未定义 */
    TF_CHECK_INT(rsexec("$c = 0\nfor 1, $nope do\n$c = $c + 1\nend"), RS_ERR_UNDEF_VAR);
    TF_CHECK_INT(rsi("c"), 1);
    /* 嵌套 + break */
    TF_CHECK_INT(rsexec("$s = 0\nfor $a = 1,2 do\nfor $b = 1,2,3 do\n"
                        "if $b == 3\nbreak\nend\n$s = $s + 1\nend\nend"), RS_OK);
    TF_CHECK_INT(rsi("s"), 4);
    /* 循环变量参与表达式 */
    TF_CHECK_INT(rsexec("$p = 1\nfor $k = 1,2,3,4 do\n$p = $p * $k\nend"), RS_OK);
    TF_CHECK_INT(rsi("p"), 24);
    TF_CHECK_INT(rsexec("for 1,2\nend"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("for do\nend"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("for 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 do\nend"), RS_ERR_LIMIT);
    TF_CHECK_INT(rsexec("for $bad = 1 do\nend\n"), RS_OK);
    tf_end();
}

static void rs_t_while(void)
{
    tf_begin("roshell:while");
    TF_CHECK_INT(rsexec("$i = 0\nwhile true\n$i = $i + 1\nif $i > 2\nbreak\nend\nend"),
                 RS_OK);
    TF_CHECK_INT(rsi("i"), 3);
    TF_CHECK_INT(rsexec("$i = 3\nwhile $i > 0\n$i = $i - 1\nend"), RS_OK);
    TF_CHECK_INT(rsi("i"), 0);
    TF_CHECK_INT(rsexec("$y = 0\nwhile false\n$y = 1\nend"), RS_OK);
    TF_CHECK_INT(rsi("y"), 0);
    /* while 内嵌套 if 的 end 不与 while 混淆 */
    TF_CHECK_INT(rsexec("$i = 0\nwhile $i < 3\nif true\n$i = $i + 1\nend\nend"), RS_OK);
    TF_CHECK_INT(rsi("i"), 3);
    tf_end();
}

static void rs_t_func(void)
{
    char buf[64];

    tf_begin("roshell:function");
    TF_CHECK_INT(rsexec("function add($a, $b)\nreturn $a + $b\nend\n"
                        "$x = add(2, 3) + 1"), RS_OK);
    TF_CHECK_INT(rsi("x"), 6);
    /* 函数作用域为脚本级：定义与调用须在同一脚本内（隐式调用语句） */
    TF_CHECK_INT(rsexec("function add($a, $b)\nreturn $a + $b\nend\nadd(1, 2)"),
                 RS_OK);
    /* 隐式调用语句丢弃返回值 */
    TF_CHECK_INT(rsexec("function five()\nreturn 5\nend\nfive()"), RS_OK);
    /* 前向引用：赋值形式 */
    TF_CHECK_INT(rsexec("$x = late()\nfunction late()\nreturn 9\nend"), RS_OK);
    TF_CHECK_INT(rsi("x"), 9);
    /* 前向引用：隐式调用语句形式 */
    TF_CHECK_INT(rsexec("later()\nfunction later()\nend"), RS_OK);
    /* 字符串返回 */
    TF_CHECK_INT(rsexec("function greet($n)\nreturn \"hi \" + $n\nend\n"
                        "$g = greet(\"bob\")"), RS_OK);
    TF_CHECK_INT(rs_get_str(g_rs, "g", buf, sizeof(buf)), RS_OK);
    TF_CHECK_STR(buf, "hi bob");
    /* 函数变量私有 */
    TF_CHECK_INT(rsexec("function setx()\n$x = 99\nend\n$x = 1\nsetx()"), RS_OK);
    TF_CHECK_INT(rsi("x"), 1);
    /* 递归深度：CALL_DEPTH=4，rec(3) 可达，rec(4) 溢出 */
    TF_CHECK_INT(rsexec("function rec($n)\nif $n > 0\nreturn rec($n - 1)\nend\n"
                        "return 0\nend\n$x = rec(3)"), RS_OK);
    TF_CHECK_INT(rsexec("function rec($n)\nif $n > 0\nreturn rec($n - 1)\nend\n"
                        "return 0\nend\n$x = rec(4)"), RS_ERR_STACK_OVERFLOW);
    /* 错误 */
    TF_CHECK_INT(rsexec("function add($a, $b)\nreturn $a + $b\nend\nadd(1)"),
                 RS_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("function add($a, $b)\nreturn $a + $b\nend\nadd(1, 2, 3)"),
                 RS_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("nope()"), RS_ERR_UNDEF_FUNC);
    TF_CHECK_INT(rsexec("function dup()\nend\nfunction dup()\nend"), RS_ERR_DUPLICATE);
    TF_CHECK_INT(rsexec("function nf()\nend\n$x = nf()"), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("function zero()\nend\nzero(1)"), RS_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("function f8($a,$b,$c,$d,$e,$f,$g,$h)\nreturn $a\nend"), RS_OK);
    TF_CHECK_INT(rsexec("function f9($a,$b,$c,$d,$e,$f,$g,$h,$i)\nend"), RS_ERR_LIMIT);
    TF_CHECK_INT(rsexec("function bad($x\nend"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("return 1"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("function ok()\nend\nok()"), RS_OK);
    tf_end();
}

static void rs_t_math(void)
{
    float fv;

    tf_begin("roshell:math");
    /* 1 参：abs 整数入返回整数、浮点入返回浮点 */
    TF_CHECK_INT(rsexec("$x = abs(-5)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 5);
    TF_CHECK_INT(rs_get_float(g_rs, "x", &fv), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = abs(-5.5)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 5.5, 1e-6);
    TF_CHECK_INT(rsexec("$x = floor(1.7)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = ceil(1.2)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 2.0, 1e-6);
    /* roundf：C99 舍入到最近整数，.5 远离零方向 */
    TF_CHECK_INT(rsexec("$x = round(2.5)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = round(-2.5)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), -3.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = sqrt(9)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = sin(0)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = cos(0)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = tan(0)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = asin(1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1.5708, 1e-4);
    TF_CHECK_INT(rsexec("$x = acos(1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = atan(1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.7854, 1e-4);
    TF_CHECK_INT(rsexec("$x = exp(0)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = log(1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = log10(100)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 2.0, 1e-6);
    /* 2 参：pow / atan2 / min / max */
    TF_CHECK_INT(rsexec("$x = pow(2, 10)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1024.0, 1e-3);
    TF_CHECK_INT(rsexec("$x = atan2(1, 1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.7854, 1e-4);
    TF_CHECK_INT(rsexec("$x = min(3, 5)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 3);
    TF_CHECK_INT(rs_get_float(g_rs, "x", &fv), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = max(3, 5)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 5);
    TF_CHECK_INT(rs_get_float(g_rs, "x", &fv), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = max(3.5, 2)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.5, 1e-6);
    /* 组合与嵌套 */
    TF_CHECK_INT(rsexec("$t = 0\n$y = sin($t) * 100"), RS_OK);
    TF_CHECK_NEAR(rsf("y"), 0.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = sqrt(pow(3, 2) + pow(4, 2))"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 5.0, 1e-6);
    /* 隐式调用语句：求值并丢弃返回值 */
    TF_CHECK_INT(rsexec("abs(2)"), RS_OK);
    /* 域错误交给 libm：sqrt(-1) 得 nan，按现有浮点值语义传播 */
    TF_CHECK_INT(rsexec("$x = sqrt(-1)"), RS_OK);
    TF_CHECK(rsf("x") != rsf("x"));
    /* 错误：实参个数不符 / 非数值实参 */
    TF_CHECK_INT(rsexec("$x = abs(1, 2)"), RS_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("$x = pow(2)"), RS_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("sqrt(1, 2)"), RS_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("$x = abs(\"a\")"), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = min(true, 1)"), RS_ERR_TYPE);
    /* math 名不允许被 function 重定义 */
    TF_CHECK_INT(rsexec("function sqrt($x)\nreturn $x\nend"), RS_ERR_DUPLICATE);
    TF_CHECK_INT(rsexec("function abs($x)\nend\n$x = abs(-1)"), RS_ERR_DUPLICATE);
    tf_end();
}

/* math 补充覆盖：边界值 / 域错误 / 类型语义 / 逐函数错误路径 / 遮蔽与组合。
 * 与 rs_t_math 已有断言不重复；nan/inf 等按实现现状断言并注释标注。 */
static void rs_t_math2(void)
{
    int32_t iv;
    int i;
    char buf[64];
    /* 1 参函数中 arity 错误路径尚未被 rs_t_math 覆盖的部分（abs/sqrt 已覆盖） */
    static const char *const one_arg[] = {
        "floor", "ceil", "round", "sin", "cos", "tan",
        "asin", "acos", "atan", "exp", "log", "log10"
    };
    /* 2 参函数中 arity 错误路径尚未被 rs_t_math 覆盖的部分（pow 已覆盖） */
    static const char *const two_arg[] = { "atan2", "min", "max" };

    tf_begin("roshell:math2");
    /* ---------- 边界值 ---------- */
    TF_CHECK_INT(rsexec("$x = abs(0)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 0);
    /* 大整数（不测 INT_MIN 溢出） */
    TF_CHECK_INT(rsexec("$x = abs(-2000000000)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 2000000000);
    TF_CHECK_INT(rsexec("$x = floor(-1.5)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), -2.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = ceil(-1.5)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), -1.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = round(1.4)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1.0, 1e-6);
    /* roundf：.5 远离零方向 */
    TF_CHECK_INT(rsexec("$x = round(-1.5)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), -2.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = sqrt(0)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = log10(1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = pow(5, 0)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1.0, 1e-6);
    /* 现状：powf(0,0) 返回 1.0（C99 推荐值） */
    TF_CHECK_INT(rsexec("$x = pow(0, 0)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1.0, 1e-6);
    TF_CHECK_INT(rsexec("$x = pow(2, -1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.5, 1e-6);
    /* atan2 轴上点：atan2(-1,0) = -pi/2，atan2(0,-1) = +pi */
    TF_CHECK_INT(rsexec("$x = atan2(-1, 0)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), -1.5707963, 1e-5);
    TF_CHECK_INT(rsexec("$x = atan2(0, -1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.1415927, 1e-5);
    TF_CHECK_INT(rsexec("$x = min(3, 3)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 3);
    TF_CHECK_INT(rsexec("$x = max(3, 3)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 3);
    TF_CHECK_INT(rsexec("$x = min(-5, -2)"), RS_OK);
    TF_CHECK_INT(rsi("x"), -5);
    TF_CHECK_INT(rsexec("$x = max(-5, -2)"), RS_OK);
    TF_CHECK_INT(rsi("x"), -2);
    /* floor/ceil/round 整数入也恒返回浮点 */
    TF_CHECK_INT(rsexec("$x = floor(3)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.0, 1e-6);
    TF_CHECK_INT(rs_get_int(g_rs, "x", &iv), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = ceil(2)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 2.0, 1e-6);
    TF_CHECK_INT(rs_get_int(g_rs, "x", &iv), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = round(2)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 2.0, 1e-6);
    TF_CHECK_INT(rs_get_int(g_rs, "x", &iv), RS_ERR_TYPE);
    /* ---------- 域错误 / 特殊值 ---------- */
    /* 现状：log(0) 得 -inf（交给 libm），按极大负值判定 */
    TF_CHECK_INT(rsexec("$x = log(0)"), RS_OK);
    TF_CHECK(rsf("x") < -1e30f);
    TF_CHECK_INT(rsexec("$x = log(-1)"), RS_OK);
    TF_CHECK(rsf("x") != rsf("x"));
    TF_CHECK_INT(rsexec("$x = asin(2)"), RS_OK);
    TF_CHECK(rsf("x") != rsf("x"));
    TF_CHECK_INT(rsexec("$x = acos(2)"), RS_OK);
    TF_CHECK(rsf("x") != rsf("x"));
    /* tan 接近 pi/2：绝对值极大的浮点数（符号随单精度参数归约，宽松边界） */
    TF_CHECK_INT(rsexec("$x = tan(1.5708)"), RS_OK);
    TF_CHECK(rsf("x") > 1e4f || rsf("x") < -1e4f);
    /* nan 参与四则运算传播 */
    TF_CHECK_INT(rsexec("$n = sqrt(-1)\n$y = $n + 1"), RS_OK);
    TF_CHECK(rsf("y") != rsf("y"));
    TF_CHECK_INT(rsexec("$y = $n - $n"), RS_OK);
    TF_CHECK(rsf("y") != rsf("y"));
    TF_CHECK_INT(rsexec("$y = $n * 0"), RS_OK);
    TF_CHECK(rsf("y") != rsf("y"));
    TF_CHECK_INT(rsexec("$y = 2 / $n"), RS_OK);
    TF_CHECK(rsf("y") != rsf("y"));
    /* 现状：== 走 rs_cmp_num，nan 的 < 与 > 均为假 → 判相等；与 > 0 / < 0 均为假 */
    TF_CHECK_INT(rsexec("$b = $n == $n"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = $n > 0"), RS_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = $n < 0"), RS_OK);
    TF_CHECK(!rsb("b"));
    /* ---------- 三角 / 反三角二次取值（单精度） ---------- */
    TF_CHECK_INT(rsexec("$x = sin(1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.84147098, 1e-6);
    TF_CHECK_INT(rsexec("$x = cos(1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 0.54030231, 1e-6);
    TF_CHECK_INT(rsexec("$x = sin(1) * sin(1) + cos(1) * cos(1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1.0, 1e-5);
    /* ---------- 类型语义反证 ---------- */
    /* abs(整数) 返回整数：结果可参与 % */
    TF_CHECK_INT(rsexec("$x = abs(-7) % 5"), RS_OK);
    TF_CHECK_INT(rsi("x"), 2);
    /* abs(浮点) 返回浮点：% 要求整数操作数 → TYPE */
    TF_CHECK_INT(rsexec("$x = abs(-5.5) % 2"), RS_ERR_TYPE);
    /* min/max 混合 int/float 提升为浮点 */
    TF_CHECK_INT(rsexec("$x = min(3.5, 2)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 2.0, 1e-6);
    TF_CHECK_INT(rs_get_int(g_rs, "x", &iv), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = max(2, 3.5)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.5, 1e-6);
    TF_CHECK_INT(rs_get_int(g_rs, "x", &iv), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = min(-2.5, -3)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), -3.0, 1e-6);
    /* ---------- 错误路径逐函数 ---------- */
    /* 1 参函数传 2 参 → ARG_COUNT */
    for (i = 0; i < (int)(sizeof(one_arg) / sizeof(one_arg[0])); i++) {
        snprintf(buf, sizeof(buf), "$x = %s(1, 2)", one_arg[i]);
        TF_CHECK_INT(rsexec(buf), RS_ERR_ARG_COUNT);
    }
    /* 2 参函数传 1 参 → ARG_COUNT */
    for (i = 0; i < (int)(sizeof(two_arg) / sizeof(two_arg[0])); i++) {
        snprintf(buf, sizeof(buf), "$x = %s(1)", two_arg[i]);
        TF_CHECK_INT(rsexec(buf), RS_ERR_ARG_COUNT);
    }
    /* 非数值实参 → TYPE（abs/min 已在 rs_t_math 覆盖） */
    TF_CHECK_INT(rsexec("$x = pow(2, \"x\")"), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = floor(\"a\")"), RS_ERR_TYPE);
    TF_CHECK_INT(rsexec("$x = max(1, \"s\")"), RS_ERR_TYPE);
    /* 查表大小写敏感：大写名不在 math 表 → undefined function */
    TF_CHECK_INT(rsexec("$x = ABS(1)"), RS_ERR_UNDEF_FUNC);
    TF_CHECK_INT(rsexec("$x = Sqrt(4)"), RS_ERR_UNDEF_FUNC);
    TF_CHECK_INT(rsexec("Max(1, 2)"), RS_ERR_UNDEF_FUNC);
    /* 现状：裸标识符不带括号不作为函数引用，按未知语句解析错误 */
    TF_CHECK_INT(rsexec("$x = abs"), RS_ERR_PARSE);
    TF_CHECK_INT(rsline("abs"), RS_ERR_PARSE);
    /* ---------- 遮蔽 ---------- */
    /* math 名不允许被 function 重定义（sqrt/abs 已在 rs_t_math 覆盖） */
    TF_CHECK_INT(rsexec("function pow($a, $b)\nreturn $a\nend"), RS_ERR_DUPLICATE);
    TF_CHECK_INT(rsexec("function min($a, $b)\nend"), RS_ERR_DUPLICATE);
    /* 非 math 名的用户函数正常定义与调用 */
    TF_CHECK_INT(rsexec("function myabs($x)\nif $x < 0\nreturn 0 - $x\nend\n"
                        "return $x\nend\n$y = myabs(-3)"), RS_OK);
    TF_CHECK_INT(rsi("y"), 3);
    /* ---------- math 调用嵌入各类语句 ---------- */
    TF_CHECK_INT(rsexec("$v = -4\nif abs($v) > 3\n$y = 1\nelse\n$y = 2\nend"), RS_OK);
    TF_CHECK_INT(rsi("y"), 1);
    TF_CHECK_INT(rsexec("$i = 3\nwhile abs($i) > 0\n$i = $i - 1\nend"), RS_OK);
    TF_CHECK_INT(rsi("i"), 0);
    TF_CHECK_INT(rsexec("$s = 0\nfor $k = abs(-1), min(4, 2) do\n$s = $s + $k\nend"),
                 RS_OK);
    TF_CHECK_INT(rsi("s"), 3);
    TF_CHECK_INT(rsexec("function hyp($a, $b)\nreturn sqrt(pow($a, 2) + pow($b, 2))\n"
                        "end\n$x = hyp(6, 8)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 10.0, 1e-5);
    TF_CHECK_INT(rsexec("$a = 3\n$b = 7\n$y = max($a, $b) * 2"), RS_OK);
    TF_CHECK_INT(rsi("y"), 14);
    /* ---------- 组合 ---------- */
    /* 勾股（变量实参形式；复用已有变量名，全局变量表上限 16 已满） */
    TF_CHECK_INT(rsexec("$a = 3\n$b = 4\n$x = sqrt(pow($a, 2) + pow($b, 2))"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 5.0, 1e-5);
    /* exp / log 互逆 */
    TF_CHECK_INT(rsexec("$g = exp(log(3))"), RS_OK);
    TF_CHECK_NEAR(rsf("g"), 3.0, 1e-5);
    /* 链式嵌套 */
    TF_CHECK_INT(rsexec("$x = floor(abs(-7.5))"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 7.0, 1e-6);
    tf_end();
}

/* 新增内置系统函数（tick/uptime/time/date/empty）与常量（PI/E）。
 * PC 测试环境假时钟未设置（hal_rtc_get_timestamp 返回 0）：
 * time() 回退为开机秒数；date() 由回退秒换算，开机 < 1 天故为 1970-01-01。
 * 顶层变量表上限 16 已满，全部复用既有变量名 */
static void rs_t_funcs(void)
{
    char buf[32];

    tf_begin("roshell:funcs");
    /* ---------- 常量：裸标识符引用，浮点值 ---------- */
    TF_CHECK_INT(rsexec("$x = PI"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.14159265f, 1e-6);
    TF_CHECK_INT(rsexec("$x = E"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 2.71828182f, 1e-6);
    TF_CHECK_INT(rsexec("$x = PI * 2"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 6.2831853f, 1e-5);
    TF_CHECK_INT(rsexec("$x = sin(PI / 2)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 1.0, 1e-6);
    /* 常量可用于条件与 for 列表 */
    TF_CHECK_INT(rsexec("$y = 0\nif PI > 3\n$y = 1\nend"), RS_OK);
    TF_CHECK_INT(rsi("y"), 1);
    TF_CHECK_INT(rsexec("$y = 0\nfor $i = 1, E do\n$y = $y + 1\nend"), RS_OK);
    TF_CHECK_INT(rsi("y"), 2);
    /* 大小写敏感：pi 不是常量 */
    TF_CHECK_INT(rsexec("$x = pi"), RS_ERR_PARSE);
    /* 裸标识符不引用 $x 变量空间（两个命名空间互不影响） */
    TF_CHECK_INT(rsexec("$x = 5\n$y = x"), RS_ERR_PARSE);
    /* 常量名保留，不允许被 function 重定义 */
    TF_CHECK_INT(rsexec("function PI()\nend"), RS_ERR_DUPLICATE);
    TF_CHECK_INT(rsexec("function E()\nend"), RS_ERR_DUPLICATE);
    /* ---------- tick / uptime ---------- */
    TF_CHECK_INT(rsexec("$t = tick()\n$i = tick()\n$b = $i >= $t"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$n = uptime()"), RS_OK);
    TF_CHECK(rsi("n") >= 0);
    TF_CHECK_INT(rsexec("$t = tick(1)"), RS_ERR_ARG_COUNT);
    /* ---------- time / date（PC：RTC 未设置 → time 回退开机秒） ---------- */
    TF_CHECK_INT(rsexec("$v = time()"), RS_OK);
    TF_CHECK(rsi("v") >= 0);
    TF_CHECK_INT(rsexec("$s = date()"), RS_OK);
    /* 回退秒 < 86400（测试在开机一天内跑完）→ 1970-01-01 */
    TF_CHECK_INT(rs_get_str(g_rs, "s", buf, sizeof(buf)), RS_OK);
    TF_CHECK(strcmp(buf, "1970-01-01") == 0);
    /* date() 返回字符串可参与拼接 */
    TF_CHECK_INT(rsexec("$s = \"d=\" + date()"), RS_OK);
    TF_CHECK_INT(rs_get_str(g_rs, "s", buf, sizeof(buf)), RS_OK);
    TF_CHECK(strcmp(buf, "d=1970-01-01") == 0);
    /* ---------- empty：任意类型判空 ---------- */
    TF_CHECK_INT(rsexec("$b = empty(0)"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = empty(1)"), RS_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = empty(0.0)"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = empty(false)"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = empty(true)"), RS_OK);
    TF_CHECK(!rsb("b"));
    TF_CHECK_INT(rsexec("$b = empty(\"\")"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$b = empty(\"a\")"), RS_OK);
    TF_CHECK(!rsb("b"));
    /* 变量实参 */
    TF_CHECK_INT(rsexec("$x = 0\n$b = empty($x)"), RS_OK);
    TF_CHECK(rsb("b"));
    TF_CHECK_INT(rsexec("$x = \"\"\n$b = empty($x)"), RS_OK);
    TF_CHECK(rsb("b"));
    /* 未定义变量：求值期即报 undefined variable（不会进入 empty） */
    TF_CHECK_INT(rsexec("$b = empty($nope)"), RS_ERR_UNDEF_VAR);
    /* 实参个数校验 */
    TF_CHECK_INT(rsexec("$b = empty()"), RS_ERR_ARG_COUNT);
    TF_CHECK_INT(rsexec("$b = empty(1, 2)"), RS_ERR_ARG_COUNT);
    /* 系统函数名保留，不允许被 function 重定义 */
    TF_CHECK_INT(rsexec("function tick()\nend"), RS_ERR_DUPLICATE);
    TF_CHECK_INT(rsexec("function empty($x)\nend"), RS_ERR_DUPLICATE);
    /* 隐式调用语句（丢弃返回值）合法 */
    TF_CHECK_INT(rsexec("tick()"), RS_OK);
    TF_CHECK_INT(rsexec("date()"), RS_OK);
    tf_end();
}

/* ------------------------------ 扩展注册辅助 ------------------------------ */

static rs_arg_t ext_add(const rs_arg_t *args, uint32_t nargs)
{
    rs_arg_t r;

    (void)nargs;
    memset(&r, 0, sizeof(r));
    if (args[0].type == RS_ARG_FLOAT || args[1].type == RS_ARG_FLOAT) {
        float a = (args[0].type == RS_ARG_FLOAT) ? args[0].f : (float)args[0].i;
        float b = (args[1].type == RS_ARG_FLOAT) ? args[1].f : (float)args[1].i;

        r.type = RS_ARG_FLOAT;
        r.f = a + b;
    } else {
        r.type = RS_ARG_INT;
        r.i = args[0].i + args[1].i;
    }
    return r;
}

static rs_arg_t ext_flag(const rs_arg_t *args, uint32_t nargs)
{
    rs_arg_t r;

    (void)args;
    (void)nargs;
    memset(&r, 0, sizeof(r));
    r.type = RS_ARG_BOOL;
    r.b = true;
    return r;
}

static rs_arg_t ext_badret(const rs_arg_t *args, uint32_t nargs)
{
    rs_arg_t r;

    (void)args;
    (void)nargs;
    memset(&r, 0, sizeof(r));
    r.type = RS_ARG_STR;      /* 非法返回类型 → type mismatch */
    r.s = "x";
    r.slen = 1U;
    return r;
}

/* 扩展注册 API：实参/返回值转换、优先级、重名与容量 */
static void rs_t_extfunc(void)
{
    char name[8];
    uint32_t i;

    tf_begin("roshell:extfunc");
    /* ---------- 注册与调用 ---------- */
    TF_CHECK_INT(rs_func_register("dadd", 2U, ext_add), RS_OK);
    TF_CHECK_INT(rs_const_register("G", 9.8), RS_OK);
    TF_CHECK_INT(rsexec("$x = dadd(2, 3)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 5);
    TF_CHECK_INT(rsexec("$x = dadd(2.5, 1)"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 3.5, 1e-6);
    /* BOOL 实参以 0/1 整数传入 */
    TF_CHECK_INT(rsexec("$x = dadd(true, 1)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 2);
    /* 字符串实参拒绝 */
    TF_CHECK_INT(rsexec("$x = dadd(\"a\", 1)"), RS_ERR_TYPE);
    /* 实参个数校验 */
    TF_CHECK_INT(rsexec("$x = dadd(1)"), RS_ERR_ARG_COUNT);
    /* 隐式调用语句 */
    TF_CHECK_INT(rsexec("dadd(1, 2)"), RS_OK);
    /* ---------- 扩展常量：裸标识符 ---------- */
    TF_CHECK_INT(rsexec("$x = G"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 9.8, 1e-6);
    TF_CHECK_INT(rsexec("$x = G * 2"), RS_OK);
    TF_CHECK_NEAR(rsf("x"), 19.6, 1e-5);
    TF_CHECK_INT(rsexec("$y = 0\nif G > 9\n$y = 1\nend"), RS_OK);
    TF_CHECK_INT(rsi("y"), 1);
    /* BOOL 返回值 */
    TF_CHECK_INT(rs_func_register("dflag", 0U, ext_flag), RS_OK);
    TF_CHECK_INT(rsexec("$b = dflag()"), RS_OK);
    TF_CHECK(rsb("b"));
    /* 非法返回值类型 */
    TF_CHECK_INT(rs_func_register("dbad", 0U, ext_badret), RS_OK);
    TF_CHECK_INT(rsexec("$b = dbad()"), RS_ERR_TYPE);
    /* ---------- 重名 / 非法名 ---------- */
    TF_CHECK_INT(rs_func_register("dadd", 1U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register("sqrt", 1U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register("tick", 0U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register("PI", 0U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register("G", 0U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_const_register("PI", 1.0), RS_ERR_PARAM);
    TF_CHECK_INT(rs_const_register("dadd", 1.0), RS_ERR_PARAM);
    TF_CHECK_INT(rs_const_register("G", 1.0), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register(NULL, 0U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register("", 0U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register("1bad", 0U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register("a-b", 0U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register("dadd", 0U, NULL), RS_ERR_PARAM);
    TF_CHECK_INT(rs_func_register("dadd", 9U, ext_add), RS_ERR_PARAM);
    TF_CHECK_INT(rs_const_register(NULL, 1.0), RS_ERR_PARAM);
    /* 扩展名保留：function 重定义被拒（登记期优先级链） */
    TF_CHECK_INT(rsexec("function dadd($a, $b)\nend"), RS_ERR_DUPLICATE);
    TF_CHECK_INT(rsexec("function G()\nend"), RS_ERR_DUPLICATE);
    /* 未注册名仍报 undefined function */
    TF_CHECK_INT(rsexec("$x = noext(1)"), RS_ERR_UNDEF_FUNC);
    /* ---------- 表满（EXT_FUNC_MAX=8：已有 dadd/dflag/dbad） ---------- */
    for (i = 0U; i < 5U; i++) {
        (void)snprintf(name, sizeof(name), "e%u", (unsigned)i);
        TF_CHECK_INT(rs_func_register(name, 2U, ext_add), RS_OK);
    }
    TF_CHECK_INT(rs_func_register("e5", 2U, ext_add), RS_ERR_LIMIT);
    TF_CHECK_INT(rsexec("$x = e0(1, 2)"), RS_OK);
    TF_CHECK_INT(rsi("x"), 3);
    /* 常量表满（EXT_CONST_MAX=8：已有 G） */
    for (i = 0U; i < 7U; i++) {
        (void)snprintf(name, sizeof(name), "c%u", (unsigned)i);
        TF_CHECK_INT(rs_const_register(name, 1.0), RS_OK);
    }
    TF_CHECK_INT(rs_const_register("c7", 1.0), RS_ERR_LIMIT);
    tf_end();
}

static void rs_t_errors(void)
{
    rs_err_t e;
    char big[RS_SCRIPT_MAX + 100U];
    char line[RS_LINE_MAX + 100U];
    char buf[4096];
    uint32_t i;
    uint32_t n = 0U;

    tf_begin("roshell:errors");
    TF_CHECK_INT(rsexec("if true"), RS_ERR_BLOCK);
    TF_CHECK_INT(rsexec("end"), RS_ERR_BLOCK);
    TF_CHECK_INT(rsexec("elseif true\nend"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("if true\nelse\nelse\nend"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("if true\nelse\nelseif false\nend"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("break"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("badcmd 5"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("wait time"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("= 5"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("$x = 1 +"), RS_ERR_PARSE);
    TF_CHECK_INT(rsexec("IF true\nend"), RS_ERR_BLOCK);
    /* 行长 / 脚本长 / 函数数 */
    for (i = 0U; i < RS_LINE_MAX + 50U; i++) {
        line[i] = 'a';
    }
    line[i] = '\0';
    TF_CHECK_INT(rsexec(line), RS_ERR_LIMIT);
    while (n + 6U < sizeof(big)) {
        memcpy(big + n, "$x=1\n;", 6U);
        n += 6U;
    }
    big[n] = '\0';
    TF_CHECK_INT(rsexec(big), RS_ERR_LIMIT);
    buf[0] = '\0';
    for (i = 0U; i < 33U; i++) {
        char fn[32];

        snprintf(fn, sizeof(fn), "function f%u()\nend\n", (unsigned)i);
        if (strlen(buf) + strlen(fn) < sizeof(buf)) {
            strcat(buf, fn);
        }
    }
    TF_CHECK_INT(rsexec(buf), RS_ERR_LIMIT);
    /* 错误行号 */
    TF_CHECK_INT(rsexec_e("$x = 1\n$y = 2\n$z = $nope\n", &e), RS_ERR_UNDEF_VAR);
    TF_CHECK_INT(e.line, 3);
    TF_CHECK_INT(rsexec_e("if true\n", &e), RS_ERR_BLOCK);
    tf_end();
}

static void rs_t_run(void)
{
    int slot;
    int i;
    int slots[RS_RUN_MAX];
    rs_arg_t args[2];

    tf_begin("roshell:run");
    rs_cmd_install(g_rs);    /* 函数体内使用 wait 命令 */
    /* 一次登记一组函数：top_prog 在 exec 结束后仍保留 */
    TF_CHECK_INT(rsexec("function fast()\nend\n"
                        "function bad()\n$x = $nope\nend\n"
                        "function slow()\nwait 200\nend\n"
                        "function two($a, $b)\nend\n"
                        "function w()\nwait 200\nend"), RS_OK);
    slot = rs_run_async(g_rs, "fast", NULL, 0);
    TF_CHECK(slot >= 0);
    TF_CHECK_INT(rs_run_state(g_rs, slot), RS_RUN_RUNNING);
    TF_CHECK_INT(rs_run_wait(g_rs, slot, ROS_TIMEOUT_FOREVER), RS_OK);
    /* 任务内错误 */
    slot = rs_run_async(g_rs, "bad", NULL, 0);
    TF_CHECK(slot >= 0);
    TF_CHECK_INT(rs_run_wait(g_rs, slot, ROS_TIMEOUT_FOREVER), RS_ERR_UNDEF_VAR);
    /* 超时后继续等待 */
    slot = rs_run_async(g_rs, "slow", NULL, 0);
    TF_CHECK(slot >= 0);
    TF_CHECK_INT(rs_run_wait(g_rs, slot, 20), RS_ERR_TIMEOUT);
    TF_CHECK_INT(rs_run_wait(g_rs, slot, ROS_TIMEOUT_FOREVER), RS_OK);
    /* 未定义函数 / 实参个数不符 / 参数非法 */
    TF_CHECK_INT(rs_run_async(g_rs, "nofunc", NULL, 0), RS_ERR_UNDEF_FUNC);
    TF_CHECK_INT(rs_run_async(g_rs, "two", NULL, 0), RS_ERR_ARG_COUNT);
    TF_CHECK_INT(rs_run_async(g_rs, "fast", NULL, 1), RS_ERR_PARAM);
    TF_CHECK_INT(rs_run_async(NULL, "fast", NULL, 0), RS_ERR_PARAM);
    /* 字面量实参（整型 + 字符串） */
    memset(args, 0, sizeof(args));
    args[0].type = RS_ARG_INT;
    args[0].i = 1;
    args[1].type = RS_ARG_STR;
    args[1].s = "ab";
    args[1].slen = 2;
    slot = rs_run_async(g_rs, "two", args, 2);
    TF_CHECK(slot >= 0);
    TF_CHECK_INT(rs_run_wait(g_rs, slot, ROS_TIMEOUT_FOREVER), RS_OK);
    /* run 内置命令（会话层分派） */
    TF_CHECK_INT(rs_cmd_dispatch(g_rs, "run fast"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(g_rs, "run nope"), RS_ERR_UNDEF_FUNC);
    /* 槽位参数 */
    TF_CHECK_INT(rs_run_wait(g_rs, -1, 0), RS_ERR_PARAM);
    TF_CHECK_INT(rs_run_state(g_rs, (int)RS_RUN_MAX), RS_ERR_PARAM);
    /* 槽位表满：先让出 CPU，使前面派生的任务结束并可回收 */
    ros_sleep_ms(50);
    for (i = 0; i < (int)RS_RUN_MAX; i++) {
        if (rs_run_state(g_rs, i) != RS_RUN_FREE) {
            (void)rs_run_wait(g_rs, i, ROS_TIMEOUT_FOREVER);
        }
    }
    for (i = 0; i < (int)RS_RUN_MAX; i++) {
        slots[i] = rs_run_async(g_rs, "w", NULL, 0);
        if (slots[i] < 0) {
            break;
        }
    }
    TF_CHECK_INT(i, (int)RS_RUN_MAX);
    TF_CHECK_INT(rs_run_async(g_rs, "w", NULL, 0), RS_ERR_LIMIT);
    TF_CHECK_INT(rs_run_count(g_rs), (int)RS_RUN_MAX);
    for (i = 0; i < (int)RS_RUN_MAX; i++) {
        TF_CHECK_INT(rs_run_wait(g_rs, slots[i], ROS_TIMEOUT_FOREVER), RS_OK);
    }
    rs_set_cmd_hook(g_rs, NULL, NULL);
    tf_end();
}

static void rs_t_exec_line(void)
{
    tf_begin("roshell:exec_line");
    TF_CHECK_INT(rsline("$a = 1"), RS_OK);
    TF_CHECK_INT(rsline("$a = $a + 1"), RS_OK);
    TF_CHECK_INT(rsi("a"), 2);
    TF_CHECK_INT(rsline("$a++"), RS_OK);
    TF_CHECK_INT(rsi("a"), 3);
    TF_CHECK_INT(rsline("if true"), RS_ERR_PARSE);
    TF_CHECK_INT(rsline("end"), RS_ERR_PARSE);
    TF_CHECK_INT(rsline("function f()"), RS_ERR_PARSE);
    TF_CHECK_INT(rsline("for $i = 1 do"), RS_ERR_PARSE);
    TF_CHECK_INT(rsline("while true"), RS_ERR_PARSE);
    TF_CHECK_INT(rsline("nope()"), RS_ERR_UNDEF_FUNC);
    TF_CHECK_INT(rsline("$a = $a + 1"), RS_OK);
    TF_CHECK_INT(rsi("a"), 4);
    tf_end();
}

/* 块内命令路由（规格 §8 statement -> command）：需命令 hook */
static void rs_t_cmd(void)
{
    tf_begin("roshell:cmd_in_script");
    rs_cmd_install(g_rs);
    /* if 块内 echo 命令 */
    TF_CHECK_INT(rsexec("if true\necho yes\nelse\necho no\nend"), RS_OK);
    /* for 块内 wait 命令 */
    TF_CHECK_INT(rsexec("for $i = 1, 2 do\nwait 5\nend"), RS_OK);
    /* 顶层命令行 */
    TF_CHECK_INT(rsexec("wait 5"), RS_OK);
    /* 旧 wait time 语句语法已删除：命令参数错误 */
    TF_CHECK_INT(rsexec("wait time 5"), -RS_ERR_PARAM);
    /* 未知命令名且非表达式：解析错误 */
    TF_CHECK_INT(rsexec("nosuchcmd arg"), RS_ERR_PARSE);
    /* 块内 run 命令：异步启动本脚本函数 */
    TF_CHECK_INT(rsexec("function job()\nend\nif true\nrun job\nend"), RS_OK);
    ros_sleep_ms(50);
    rs_set_cmd_hook(g_rs, NULL, NULL);
    tf_end();
}

static void rs_driver_entry(void *arg)
{
    (void)arg;

    g_rs = rs_create();
    TF_CHECK(g_rs != NULL);
    if (g_rs != NULL) {
        rs_t_lifecycle();
        rs_t_arith();
        rs_t_logic();
        rs_t_string();
        rs_t_vars();
        rs_t_if();
        rs_t_for();
        rs_t_while();
        rs_t_func();
        rs_t_math();
        rs_t_math2();
        rs_t_funcs();
        rs_t_extfunc();
        rs_t_errors();
        rs_t_run();
        rs_t_exec_line();
        rs_t_cmd();
        ros_sleep_ms(100);       /* 让 run 命令派生的任务全部结束 */
        rs_destroy(g_rs);
        g_rs = NULL;
    }
    ros_stop();
}

void ut_roshell(void)
{
    ros_init();

    TF_CHECK_INT(ros_task_create(&s_roshell_driver, "rosh_ut", 0,
                                 rs_driver_entry, NULL,
                                 s_roshell_stack, sizeof(s_roshell_stack)),
                 ROS_OK);

    ros_sched_start();
}
