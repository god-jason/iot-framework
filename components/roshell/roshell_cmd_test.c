/*
 * roshell_cmd_test.c - roshell 命令集与交互会话单元测试（ut_roshell_cmd）
 *
 * 覆盖：
 *   - roshell_cmd_dispatch：内建命令（help/echo/ps/jobs/mem/state/ver）、
 *     cmd 全局命令表回退、脚本引擎回退、参数错误路径；
 *   - roshell_shell_feed 组行：分片投递、CRLF 归一、超长行截断、
 *     Ctrl-C 清行；
 *   - 会话生命周期：start / process / exit / task_finished / shutdown。
 *
 * 模式与 roshell_test.c 一致：用例在 driver 任务内执行。
 * 会话输入任务（prio 4）在 driver 不睡眠时不会被调度，
 * 组行行为完全由测试手动调用 roshell_shell_process() 驱动，结果确定。
 */

#include "test.h"
#include "ros.h"
#include "common.h"
#include "cmd.h"
#include "roshell_cmd.h"

#include <stdio.h>
#include <string.h>

static ros_task_t s_rcmd_driver;
static uint8_t s_rcmd_stack[8192];

/* ---------------- cmd 全局表回退验证 ---------------- */

static bool s_rct_hit;

static int rct_handler(const args_parsed_t *parsed, void *user)
{
    (void)parsed;
    (void)user;
    s_rct_hit = true;
    return 0;
}

static const cmd_def_t s_rct_def = {
    "rctest", "roshell_cmd test hook", NULL, 0U, NULL, NULL, NULL
};

/* ---------------- 命令 dispatch ---------------- */

static void ro_t_dispatch(roshell_t *rs)
{
    int32_t v;

    tf_begin("roshell_cmd:dispatch");

    /* 空行与 NULL */
    TF_CHECK_INT(roshell_cmd_dispatch(rs, ""), RO_OK);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, NULL), RO_ERR_PARAM);

    /* 内建命令：不崩溃、返回 0 */
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "help"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "help ps"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "help nosuchcmd"), -RO_ERR_NOT_FOUND);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "ver"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "echo hello roshell"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "ps"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "jobs"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "mem"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "state"), 0);

    /* 大小写敏感：PS 不是命令，落入脚本路径后报语法错误 */
    TF_CHECK(roshell_cmd_dispatch(rs, "PS") < 0);

    /* 参数缺失 */
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "kill"), -RO_ERR_PARAM);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "waitjob"), -RO_ERR_PARAM);

    /* 任务查找：不存在 / 保护当前任务 */
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "kill nosuch_task"), -RO_ERR_NOT_FOUND);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "kill rosh_ut_cmd"), -RO_ERR_PARAM);

    /* 脚本路径：赋值 / 表达式语句 / 语法错误 */
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "$x = 40 + 2"), RO_OK);
    v = 0;
    TF_CHECK_INT(roshell_get_int(rs, "x", &v), RO_OK);
    TF_CHECK_INT(v, 42);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "1 + 2"), RO_OK);
    TF_CHECK(roshell_cmd_dispatch(rs, "zzzq") < 0);

    /* echo 的 $var 替换（变量存在与不存在都不崩溃） */
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "echo $x"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "echo $no_such_var"), 0);

    /* 脚本路径：exec_line 只接受单行语句，多行块报语法错 */
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "$y = $x * 2"), RO_OK);
    v = 0;
    TF_CHECK_INT(roshell_get_int(rs, "y", &v), RO_OK);
    TF_CHECK_INT(v, 84);
    TF_CHECK_INT(roshell_cmd_dispatch(rs, "if $x > 40 then"), RO_ERR_PARSE);
    tf_end();
}

static void ro_t_no_instance(void)
{
    tf_begin("roshell_cmd:no_instance");
    /* rs 为 NULL：命令可用，脚本路径报参数错 */
    TF_CHECK_INT(roshell_cmd_dispatch(NULL, "help"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(NULL, "ps"), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(NULL, "$x = 1"), -RO_ERR_PARAM);
    tf_end();
}

static void ro_t_cmdtable(void)
{
    tf_begin("roshell_cmd:cmdtable");
    s_rct_hit = false;
    TF_CHECK_INT(cmd_register(&s_rct_def, rct_handler, NULL), 0);
    TF_CHECK_INT(roshell_cmd_dispatch(NULL, "rctest"), 0);
    TF_CHECK(s_rct_hit);
    tf_end();
}

/* ---------------- 会话 feed 组行与生命周期 ---------------- */

static void ro_t_session(void)
{
    char pad[64];
    uint32_t i;
    int32_t v;

    tf_begin("roshell_cmd:session");

    /* 未启动：feed 丢弃，process 返回 0 */
    roshell_shell_feed("help\n", 5U);
    TF_CHECK(!roshell_shell_started());
    TF_CHECK_INT(roshell_shell_process(), 0U);
    TF_CHECK(roshell_shell_instance() == NULL);

    /* 启动 */
    TF_CHECK_INT(roshell_shell_start("ut"), 0);
    TF_CHECK(roshell_shell_started());
    TF_CHECK(roshell_shell_instance() != NULL);
    TF_CHECK(!roshell_shell_task_finished());
    /* 幂等 */
    TF_CHECK_INT(roshell_shell_start("ut"), 0);

    /* 分片投递组行："$x = 4" + "1+1\r\n" -> 一行脚本 */
    roshell_shell_feed("$x = 4", 6U);
    TF_CHECK_INT(roshell_shell_process(), 0U);          /* 未提交 */
    roshell_shell_feed("1+1\r\n", 5U);
    TF_CHECK_INT(roshell_shell_process(), 1U);          /* 提交 1 行 */
    v = 0;
    TF_CHECK_INT(roshell_get_int(roshell_shell_instance(), "x", &v), RO_OK);
    TF_CHECK_INT(v, 42);

    /* CRLF 只算一次提交 */
    roshell_shell_feed("echo a\r\n", 8U);
    TF_CHECK_INT(roshell_shell_process(), 1U);
    /* 连续 CR CR：第二次 CR 提交空行 */
    roshell_shell_feed("echo b\r\r", 8U);
    TF_CHECK_INT(roshell_shell_process(), 2U);

    /* 超长行截断到 ROSHELL_LINE_MAX-1（逐片投喂避免环形缓冲溢出） */
    memset(pad, 'a', sizeof(pad));
    for (i = 0U; i < 6U; i++) {
        roshell_shell_feed(pad, sizeof(pad));           /* 每次 64 字节 */
        TF_CHECK_INT(roshell_shell_process(), 0U);
    }
    TF_CHECK_INT(roshell_shell_line_pending(), (uint32_t)ROSHELL_LINE_MAX - 1U);
    roshell_shell_feed("\n", 1U);
    TF_CHECK_INT(roshell_shell_process(), 1U);
    TF_CHECK_INT(roshell_shell_line_pending(), 0U);

    /* Ctrl-C：清行且不提交 */
    roshell_shell_feed("abc", 3U);
    TF_CHECK_INT(roshell_shell_process(), 0U);
    TF_CHECK_INT(roshell_shell_line_pending(), 3U);
    roshell_shell_feed("\x03", 1U);
    TF_CHECK_INT(roshell_shell_process(), 0U);
    TF_CHECK_INT(roshell_shell_line_pending(), 0U);

    /* 退格编辑："$z = 1" 退格删 1 再输入 2 -> $z = 2 */
    roshell_shell_feed("$z = 1\x08" "2\r", 9U);
    TF_CHECK_INT(roshell_shell_process(), 1U);
    v = 0;
    TF_CHECK_INT(roshell_get_int(roshell_shell_instance(), "z", &v), RO_OK);
    TF_CHECK_INT(v, 2);

    /* kill 保护会话任务 */
    TF_CHECK_INT(roshell_cmd_dispatch(roshell_shell_instance(),
                                      "kill roshell_io"), -RO_ERR_PARAM);

    /* exit：提交后 task_finished，shutdown 回收 */
    roshell_shell_feed("exit\n", 5U);
    TF_CHECK_INT(roshell_shell_process(), 1U);
    TF_CHECK(roshell_shell_task_finished());
    TF_CHECK_INT(roshell_shell_shutdown(), 0);
    TF_CHECK(!roshell_shell_started());
    TF_CHECK(roshell_shell_instance() == NULL);
    /* 未启动时 shutdown 失败 */
    TF_CHECK_INT(roshell_shell_shutdown(), -1);

    /* request_exit 路径：空缓冲也能退出 */
    TF_CHECK_INT(roshell_shell_start(NULL), 0);
    roshell_shell_request_exit();
    TF_CHECK_INT(roshell_shell_process(), 0U);
    TF_CHECK(roshell_shell_task_finished());
    TF_CHECK_INT(roshell_shell_shutdown(), 0);
    tf_end();
}

/* ---------------- driver ---------------- */

static void roshell_cmd_driver_entry(void *arg)
{
    roshell_t *rs;

    (void)arg;

    rs = roshell_create();
    TF_CHECK(rs != NULL);
    if (rs != NULL) {
        ro_t_dispatch(rs);
        roshell_destroy(rs);
    }
    ro_t_no_instance();
    ro_t_cmdtable();
    ro_t_session();
    ros_stop();
}

void ut_roshell_cmd(void)
{
    ros_init();

    TF_CHECK_INT(ros_task_create(&s_rcmd_driver, "rosh_ut_cmd", 0,
                                 roshell_cmd_driver_entry, NULL,
                                 s_rcmd_stack, sizeof(s_rcmd_stack)),
                 ROS_OK);

    ros_sched_start();
}
