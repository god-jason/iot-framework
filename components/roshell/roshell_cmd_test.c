/*
 * roshell_cmd_test.c - roshell 命令集与交互会话单元测试（ut_roshell_cmd）
 *
 * 覆盖：
 *   - rs_cmd_dispatch：内建命令（help/echo/ps/jobs/mem/state/ver）、
 *     rs_cmd 全局命令表回退、脚本引擎回退、参数错误路径；
 *   - rs_shell_feed 组行：分片投递、CRLF 归一、超长行截断、
 *     Ctrl-C 清行；
 *   - 会话生命周期：start / process / exit / task_finished / shutdown。
 *
 * 模式与 roshell_test.c 一致：用例在 driver 任务内执行。
 * 会话输入任务（prio 4）在 driver 不睡眠时不会被调度，
 * 组行行为完全由测试手动调用 rs_shell_process() 驱动，结果确定。
 */

#include "test.h"
#include "ros.h"
#include "common.h"
#include "roshell.h"
#include "roshell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ros_task_t s_rcmd_driver;
static uint8_t s_rcmd_stack[8192];

/* ---------------- rs_cmd 全局表回退验证 ---------------- */

static bool s_rct_hit;

static int rct_handler(const rs_cmd_ctx_t *ctx)
{
    (void)ctx;
    s_rct_hit = true;
    return 0;
}

static const rs_cmd_t s_rct_def = {
    "rctest", "roshell_cmd test hook", NULL, NULL, NULL, 0U,
    rct_handler, NULL, NULL, NULL, NULL
};

/* ---------------- 命令 dispatch ---------------- */

static void rs_t_dispatch(rs_t *rs)
{
    int32_t v;

    tf_begin("roshell_cmd:dispatch");

    /* 空行与 NULL */
    TF_CHECK_INT(rs_cmd_dispatch(rs, ""), RS_OK);
    TF_CHECK_INT(rs_cmd_dispatch(rs, NULL), RS_ERR_PARAM);

    /* 内建命令：不崩溃、返回 0 */
    TF_CHECK_INT(rs_cmd_dispatch(rs, "help"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "help ps"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "help nosuchcmd"), -RS_ERR_NOT_FOUND);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "ver"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "echo hello roshell"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "ps"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "jobs"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "mem"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "state"), 0);

    /* 大小写敏感：PS 不是命令，落入脚本路径后报语法错误 */
    TF_CHECK(rs_cmd_dispatch(rs, "PS") < 0);

    /* 参数缺失 */
    TF_CHECK_INT(rs_cmd_dispatch(rs, "kill"), -RS_ERR_PARAM);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "waitjob"), -RS_ERR_PARAM);

    /* 任务查找：不存在 / 保护当前任务 */
    TF_CHECK_INT(rs_cmd_dispatch(rs, "kill nosuch_task"), -RS_ERR_NOT_FOUND);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "kill rosh_ut_cmd"), -RS_ERR_PARAM);

    /* 脚本路径：赋值 / 表达式语句 / 语法错误 */
    TF_CHECK_INT(rs_cmd_dispatch(rs, "$x = 40 + 2"), RS_OK);
    v = 0;
    TF_CHECK_INT(rs_get_int(rs, "x", &v), RS_OK);
    TF_CHECK_INT(v, 42);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "1 + 2"), RS_OK);
    TF_CHECK(rs_cmd_dispatch(rs, "zzzq") < 0);

    /* echo 的 $var 替换（变量存在与不存在都不崩溃） */
    TF_CHECK_INT(rs_cmd_dispatch(rs, "echo $x"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "echo $no_such_var"), 0);

    /* 脚本路径：exec_line 只接受单行语句，多行块报语法错 */
    TF_CHECK_INT(rs_cmd_dispatch(rs, "$y = $x * 2"), RS_OK);
    v = 0;
    TF_CHECK_INT(rs_get_int(rs, "y", &v), RS_OK);
    TF_CHECK_INT(v, 84);
    TF_CHECK_INT(rs_cmd_dispatch(rs, "if $x > 40 then"), RS_ERR_PARSE);
    tf_end();
}

static void rs_t_no_instance(void)
{
    tf_begin("roshell_cmd:no_instance");
    /* rs 为 NULL：命令可用，脚本路径报参数错 */
    TF_CHECK_INT(rs_cmd_dispatch(NULL, "help"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(NULL, "ps"), 0);
    TF_CHECK_INT(rs_cmd_dispatch(NULL, "$x = 1"), -RS_ERR_PARAM);
    tf_end();
}

static void rs_t_cmdtable(void)
{
    tf_begin("roshell_cmd:cmdtable");
    s_rct_hit = false;
    TF_CHECK_INT(rs_cmd_register(&s_rct_def), 0);
    TF_CHECK_INT(rs_cmd_dispatch(NULL, "rctest"), 0);
    TF_CHECK(s_rct_hit);
    tf_end();
}

/* ---------------- 会话 feed 组行与生命周期 ---------------- */

static void rs_t_session(void)
{
    char pad[64];
    uint32_t i;
    int32_t v;

    tf_begin("roshell_cmd:session");

    /* 未启动：feed 丢弃，process 返回 0 */
    rs_shell_feed("help\n", 5U);
    TF_CHECK(!rs_shell_started());
    TF_CHECK_INT(rs_shell_process(), 0U);
    TF_CHECK(rs_shell_instance() == NULL);

    /* 启动 */
    TF_CHECK_INT(rs_shell_start("ut"), 0);
    TF_CHECK(rs_shell_started());
    TF_CHECK(rs_shell_instance() != NULL);
    TF_CHECK(!rs_shell_task_finished());
    /* 幂等 */
    TF_CHECK_INT(rs_shell_start("ut"), 0);

    /* 分片投递组行："$x = 4" + "1+1\r\n" -> 一行脚本 */
    rs_shell_feed("$x = 4", 6U);
    TF_CHECK_INT(rs_shell_process(), 0U);          /* 未提交 */
    rs_shell_feed("1+1\r\n", 5U);
    TF_CHECK_INT(rs_shell_process(), 1U);          /* 提交 1 行 */
    v = 0;
    TF_CHECK_INT(rs_get_int(rs_shell_instance(), "x", &v), RS_OK);
    TF_CHECK_INT(v, 42);

    /* CRLF 只算一次提交 */
    rs_shell_feed("echo a\r\n", 8U);
    TF_CHECK_INT(rs_shell_process(), 1U);
    /* 连续 CR CR：第二次 CR 提交空行 */
    rs_shell_feed("echo b\r\r", 8U);
    TF_CHECK_INT(rs_shell_process(), 2U);

    /* 超长行截断到 RS_LINE_MAX-1（逐片投喂避免环形缓冲溢出） */
    memset(pad, 'a', sizeof(pad));
    for (i = 0U; i < 6U; i++) {
        rs_shell_feed(pad, sizeof(pad));           /* 每次 64 字节 */
        TF_CHECK_INT(rs_shell_process(), 0U);
    }
    TF_CHECK_INT(rs_shell_line_pending(), (uint32_t)RS_LINE_MAX - 1U);
    rs_shell_feed("\n", 1U);
    TF_CHECK_INT(rs_shell_process(), 1U);
    TF_CHECK_INT(rs_shell_line_pending(), 0U);

    /* Ctrl-C：清行且不提交 */
    rs_shell_feed("abc", 3U);
    TF_CHECK_INT(rs_shell_process(), 0U);
    TF_CHECK_INT(rs_shell_line_pending(), 3U);
    rs_shell_feed("\x03", 1U);
    TF_CHECK_INT(rs_shell_process(), 0U);
    TF_CHECK_INT(rs_shell_line_pending(), 0U);

    /* 退格编辑："$z = 1" 退格删 1 再输入 2 -> $z = 2 */
    rs_shell_feed("$z = 1\x08" "2\r", 9U);
    TF_CHECK_INT(rs_shell_process(), 1U);
    v = 0;
    TF_CHECK_INT(rs_get_int(rs_shell_instance(), "z", &v), RS_OK);
    TF_CHECK_INT(v, 2);

    /* kill 保护会话任务 */
    TF_CHECK_INT(rs_cmd_dispatch(rs_shell_instance(),
                                      "kill roshell_io"), -RS_ERR_PARAM);

    /* exit：提交后 task_finished，shutdown 回收 */
    rs_shell_feed("exit\n", 5U);
    TF_CHECK_INT(rs_shell_process(), 1U);
    TF_CHECK(rs_shell_task_finished());
    TF_CHECK_INT(rs_shell_shutdown(), 0);
    TF_CHECK(!rs_shell_started());
    TF_CHECK(rs_shell_instance() == NULL);
    /* 未启动时 shutdown 失败 */
    TF_CHECK_INT(rs_shell_shutdown(), -1);

    /* request_exit 路径：空缓冲也能退出 */
    TF_CHECK_INT(rs_shell_start(NULL), 0);
    rs_shell_request_exit();
    TF_CHECK_INT(rs_shell_process(), 0U);
    TF_CHECK(rs_shell_task_finished());
    TF_CHECK_INT(rs_shell_shutdown(), 0);
    tf_end();
}

/* ---------------- wait 暂停/恢复时序 ---------------- */
/*
 * worker 任务执行内建命令 wait <ms>（经 rs_cmd_dispatch，验证
 * 内建命令 active 登记路径），driver 在暂停/恢复时刻用 ros_tick_get()
 * 采样。主机单元测试为模拟时间（1 tick = 1ms，空闲任务批量推进），
 * 时序断言按内核毫秒判定，结果确定。
 */

static ros_task_t s_wait_worker;
static uint8_t    s_wait_stack[4096];
static uint32_t   s_wait_ms;
static volatile bool     s_wait_done;
static volatile uint32_t s_wait_done_tick;

static void wait_worker_entry(void *arg)
{
    char line[32];

    (void)arg;
    (void)snprintf(line, sizeof(line), "wait %lu", (unsigned long)s_wait_ms);
    (void)rs_cmd_dispatch(NULL, line);
    s_wait_done_tick = ros_tick_get();
    s_wait_done = true;
    ros_task_exit();
}

/* 缩放时序：wait 1000，300ms 后暂停，再 300ms 后恢复，
 * 总时长 ≈1300ms（等待 1000 + 停放 300），恢复后只等剩余 700ms。 */
static void rs_t_wait_pause(void)
{
    uint32_t t0;
    uint32_t t_pause;
    uint32_t t_resume;
    uint32_t total;
    uint32_t remaining;
    uint32_t guard;

    tf_begin("roshell_cmd:wait_pause");

    s_wait_ms = 1000U;
    s_wait_done = false;
    s_wait_done_tick = 0U;
    t0 = ros_tick_get();
    TF_CHECK_INT(ros_task_create(&s_wait_worker, "rosh_ut_wait", 5U,
                                 wait_worker_entry, NULL,
                                 s_wait_stack, sizeof(s_wait_stack)),
                 ROS_OK);

    ros_sleep_ms(300U);
    t_pause = ros_tick_get();
    TF_CHECK_INT(rs_cmd_notify_pause(&s_wait_worker), 0);
    ros_sleep_ms(300U);
    t_resume = ros_tick_get();
    TF_CHECK_INT(rs_cmd_notify_resume(&s_wait_worker), 0);

    guard = t0 + 5000U;
    while (!s_wait_done && ROS_TICK_AFTER(guard, ros_tick_get())) {
        ros_sleep_ms(10U);
    }
    TF_CHECK(s_wait_done);
    total = s_wait_done_tick - t0;
    remaining = s_wait_done_tick - t_resume;
    /* 总时长 ≈1300ms（±250）；暂停 300ms 不消耗等待：剩余 ≈700ms */
    TF_CHECK(total >= 1050U && total <= 1550U);
    TF_CHECK(remaining >= 450U && remaining <= 950U);
    printf("    wait_pause: pause@%lums resume@%lums total=%lums remain=%lums\n",
           (unsigned long)(t_pause - t0), (unsigned long)(t_resume - t0),
           (unsigned long)total, (unsigned long)remaining);
    tf_end();
}

/* 真实尺度用例（默认跳过）：RS_WAIT_TEST=1 启用。
 * wait 10000，5s 时暂停，5s 后恢复，验证剩余 ~5s 被正确等待。
 * 用 pause/resume 内建命令端到端驱动（验证双路分派）。 */
static void rs_t_wait_pause_10s(void)
{
    const char *env = getenv("ROSHELL_WAIT_TEST");
    uint32_t t0;
    uint32_t t_pause;
    uint32_t t_resume;
    uint32_t total;
    uint32_t remaining;
    uint32_t guard;

    if (env == NULL || env[0] == '\0' || env[0] == '0') {
        return;
    }

    tf_begin("roshell_cmd:wait_pause_10s");

    s_wait_ms = 10000U;
    s_wait_done = false;
    s_wait_done_tick = 0U;
    t0 = ros_tick_get();
    TF_CHECK_INT(ros_task_create(&s_wait_worker, "rosh_ut_wait", 5U,
                                 wait_worker_entry, NULL,
                                 s_wait_stack, sizeof(s_wait_stack)),
                 ROS_OK);

    ros_sleep_ms(5000U);
    t_pause = ros_tick_get();
    TF_CHECK_INT(rs_cmd_dispatch(NULL, "pause rosh_ut_wait"), 0);
    ros_sleep_ms(5000U);
    t_resume = ros_tick_get();
    TF_CHECK_INT(rs_cmd_dispatch(NULL, "resume rosh_ut_wait"), 0);

    guard = t0 + 20000U;
    while (!s_wait_done && ROS_TICK_AFTER(guard, ros_tick_get())) {
        ros_sleep_ms(10U);
    }
    TF_CHECK(s_wait_done);
    total = s_wait_done_tick - t0;
    remaining = s_wait_done_tick - t_resume;
    /* 恢复后剩余 4.5~6s；总时长 14.5~16s（等待 10s + 停放 5s） */
    TF_CHECK(remaining >= 4500U && remaining <= 6000U);
    TF_CHECK(total >= 14500U && total <= 16000U);
    printf("    wait10s: pause@%lums resume@%lums total=%lums remain=%lums\n",
           (unsigned long)(t_pause - t0), (unsigned long)(t_resume - t0),
           (unsigned long)total, (unsigned long)remaining);
    tf_end();
}

/* ---------------- driver ---------------- */

static void rs_cmd_driver_entry(void *arg)
{
    rs_t *rs;

    (void)arg;

    rs = rs_create();
    TF_CHECK(rs != NULL);
    if (rs != NULL) {
        rs_t_dispatch(rs);
        rs_destroy(rs);
    }
    rs_t_no_instance();
    rs_t_cmdtable();
    rs_t_session();
    rs_t_wait_pause();
    rs_t_wait_pause_10s();
    ros_stop();
}

void ut_roshell_cmd(void)
{
    ros_init();

    TF_CHECK_INT(ros_task_create(&s_rcmd_driver, "rosh_ut_cmd", 0,
                                 rs_cmd_driver_entry, NULL,
                                 s_rcmd_stack, sizeof(s_rcmd_stack)),
                 ROS_OK);

    ros_sched_start();
}
