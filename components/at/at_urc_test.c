/* components/modem/ml307/at_urc.c per-file 单元测试。
 *
 * 覆盖：注册/注销参数校验、前缀匹配（长度、大小写、边界）、
 *       匹配优先级、空前缀、回调参数、8 槽位容量上限与注销后复用。
 * 未覆盖：无（模块无更多公开行为）。
 */
#include "test.h"
#include "at_urc.h"

#include <string.h>

static int s_ca_calls;
static int s_cb_calls;
static char s_last_line[128];
static uint32_t s_last_len;
static void *s_last_user;

static void reset_rec(void)
{
    s_ca_calls = 0;
    s_cb_calls = 0;
    s_last_line[0] = '\0';
    s_last_len = 0;
    s_last_user = NULL;
}

static void cb_a(const char *line, uint32_t len, void *user)
{
    uint32_t n;

    s_ca_calls++;
    s_last_user = user;
    s_last_len = len;
    n = len;
    if (n > sizeof(s_last_line) - 1) {
        n = sizeof(s_last_line) - 1;
    }
    if (line != NULL) {
        memcpy(s_last_line, line, n);
    }
    s_last_line[n] = '\0';
}

static void cb_b(const char *line, uint32_t len, void *user)
{
    (void)line;
    (void)len;
    (void)user;
    s_cb_calls++;
}

static void unregister_all(void)
{
    char name[8];
    int i;

    (void)at_urc_unregister("RING");
    (void)at_urc_unregister("AB");
    (void)at_urc_unregister("A");
    (void)at_urc_unregister("");
    for (i = 0; i < 9; i++) {
        name[0] = 'P';
        name[1] = (char)('0' + i);
        name[2] = '\0';
        (void)at_urc_unregister(name);
    }
}

static void test_register(void)
{
    unregister_all();
    reset_rec();

    tf_begin("at_urc/注册参数校验");
    TF_CHECK_INT(at_urc_register(NULL, cb_a, NULL), -1);
    TF_CHECK_INT(at_urc_register("RING", NULL, NULL), -1);
    TF_CHECK_INT(at_urc_register("RING", cb_a, (void *)0x1234), 0);
    tf_end();

    tf_begin("at_urc/前缀匹配与回调参数");
    reset_rec();
    TF_CHECK_INT(at_urc_dispatch("RING OK", 7), 0);
    TF_CHECK_INT(s_ca_calls, 1);
    TF_CHECK_INT(s_last_len, 7);
    TF_CHECK_STR(s_last_line, "RING OK");
    TF_CHECK(s_last_user == (void *)0x1234);
    tf_end();

    tf_begin("at_urc/不匹配返回 -1");
    reset_rec();
    TF_CHECK_INT(at_urc_dispatch("NOISE", 5), -1);
    TF_CHECK_INT(s_ca_calls, 0);
    TF_CHECK_INT(at_urc_dispatch("RIN", 3), -1);      /* 长度不足 */
    TF_CHECK_INT(at_urc_dispatch("ring", 4), -1);     /* 大小写敏感 */
    TF_CHECK_INT(s_ca_calls, 0);
    tf_end();

    tf_begin("at_urc/前缀恰好等长与前缀后带内容");
    reset_rec();
    TF_CHECK_INT(at_urc_dispatch("RING", 4), 0);
    TF_CHECK_INT(s_ca_calls, 1);
    TF_CHECK_INT(at_urc_dispatch("RINGX", 5), 0);
    TF_CHECK_INT(s_ca_calls, 2);
    tf_end();

    tf_begin("at_urc/空行与 NULL");
    reset_rec();
    TF_CHECK_INT(at_urc_dispatch(NULL, 0), -1);
    TF_CHECK_INT(at_urc_dispatch("", 0), -1);
    TF_CHECK_INT(s_ca_calls, 0);
    tf_end();

    tf_begin("at_urc/注销");
    TF_CHECK_INT(at_urc_unregister(NULL), -1);
    TF_CHECK_INT(at_urc_unregister("RING"), 0);
    TF_CHECK_INT(at_urc_dispatch("RING OK", 7), -1);
    TF_CHECK_INT(at_urc_unregister("RING"), -1);      /* 重复注销失败 */
    tf_end();
}

static void test_order_and_empty(void)
{
    unregister_all();
    reset_rec();

    tf_begin("at_urc/按注册顺序取第一个匹配");
    TF_CHECK_INT(at_urc_register("AB", cb_a, NULL), 0);
    TF_CHECK_INT(at_urc_register("A", cb_b, NULL), 0);
    reset_rec();
    TF_CHECK_INT(at_urc_dispatch("ABC", 3), 0);
    TF_CHECK_INT(s_ca_calls, 1);
    TF_CHECK_INT(s_cb_calls, 0);
    (void)at_urc_unregister("AB");
    (void)at_urc_unregister("A");
    tf_end();

    tf_begin("at_urc/空前缀匹配任意行");
    reset_rec();
    TF_CHECK_INT(at_urc_register("", cb_a, NULL), 0);
    TF_CHECK_INT(at_urc_dispatch("anything", 8), 0);
    TF_CHECK_INT(s_ca_calls, 1);
    TF_CHECK_INT(s_last_len, 8);
    TF_CHECK_INT(at_urc_unregister(""), 0);
    tf_end();
}

static void test_capacity(void)
{
    char name[8];
    int i;

    unregister_all();
    reset_rec();

    tf_begin("at_urc/最多注册 8 个前缀");
    for (i = 0; i < 8; i++) {
        name[0] = 'P';
        name[1] = (char)('0' + i);
        name[2] = '\0';
        TF_CHECK_INT(at_urc_register(name, cb_b, NULL), 0);
    }
    TF_CHECK_INT(at_urc_register("P8", cb_b, NULL), -1);
    tf_end();

    tf_begin("at_urc/注销后可再次注册");
    for (i = 0; i < 8; i++) {
        name[0] = 'P';
        name[1] = (char)('0' + i);
        name[2] = '\0';
        TF_CHECK_INT(at_urc_unregister(name), 0);
    }
    TF_CHECK_INT(at_urc_register("P8", cb_b, NULL), 0);
    TF_CHECK_INT(at_urc_unregister("P8"), 0);
    tf_end();
}

void ut_at_urc(void)
{
    test_register();
    test_order_and_empty();
    test_capacity();
}
