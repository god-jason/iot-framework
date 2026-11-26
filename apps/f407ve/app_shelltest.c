/*
 * app_shelltest.c - roshell 长命令生命周期上板自检
 *
 * 通过 rs_cmd_dispatch 走真实命令分派路径，验证：
 *   1) '-' 异步长命令立即返回，wait 能等待其结束；
 *   2) wait timeout 在长命令未结束时返回超时；
 *   3) sleep 同步长命令按时结束；
 *   4) pause/resume 期间不消耗 sleep 时间（deadline 顺延）。
 *
 * 自检在 app_main 启动阶段同步执行（压测/文件系统之前），
 * 结果以 [I][sht] PASS/FAIL 打印到串口。
 */

#include "app_shelltest.h"

#include "ros.h"
#include "log.h"
#include "strings.h"
#include "roshell.h"

/* 各用例时长（ms）与允许误差 */
#define ST_ASYNC_MS      800U
#define ST_SYNC_MS       300U
#define ST_WORK_MS       900U
#define ST_PAUSE_AT      300U
#define ST_PAUSED_MS     500U

#define ST_LO(v, a, b)   ((v) >= (a) && (v) <= (b))

static uint32_t s_fail;

/* 自检使用的 roshell 实例：sleep 等长命令状态登记在实例上下文里 */
static roshell_t *s_rs;

/* ------------------------------ 小工具 ------------------------------ */

static void st_check(const char *what, bool ok, uint32_t val)
{
    if (ok) {
        LOG_I("sht", "PASS %s (%u ms)", what, (unsigned)val);
    } else {
        s_fail++;
        LOG_E("sht", "FAIL %s (%u ms)", what, (unsigned)val);
    }
}

/* ------------------------------ pause 用例 worker ------------------------------ */

static volatile bool s_wt_done;
static volatile bool s_wt_started;
static volatile uint32_t s_wt_end;

static void st_worker_entry(void *arg)
{
    char line[32];
    int rc;

    (void)arg;
    (void)str_printf(line, sizeof(line), "sleep %u", (unsigned)ST_WORK_MS);
    LOG_I("sht", "worker start t=%u", (unsigned)ros_tick_get());
    s_wt_started = true;
    rc = rs_cmd_dispatch(s_rs, line);       /* 同步长命令：内部轮转 on_tick */
    LOG_I("sht", "worker dispatch rc=%d t=%u", rc, (unsigned)ros_tick_get());
    s_wt_end = ros_tick_get();
    s_wt_done = true;
    ros_task_exit();
}

/* ------------------------------ 用例 ------------------------------ */

/* 1) '-' 异步启动 + wait 等待 + timeout */
static void st_test_async(void)
{
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    int rc;

    t0 = ros_tick_get();
    rc = rs_cmd_dispatch(s_rs, "-sleep 800");
    t1 = ros_tick_get();
    st_check("async start non-blocking", (rc == RS_OK) && ((t1 - t0) <= 50U),
             t1 - t0);

    /* 让时间走 200ms：异步命令应仍在运行 -> wait timeout 命中 */
    ros_sleep_ms(200U);
    rc = rs_cmd_dispatch(s_rs, "wait timeout 100");
    st_check("wait timeout while running", (rc == -RS_ERR_TIMEOUT), 0U);

    /* 阻塞到异步命令结束；从启动到结束应≈800ms */
    rc = rs_cmd_dispatch(s_rs, "wait");
    t2 = ros_tick_get();
    st_check("wait blocks until done",
             (rc == RS_OK) && ST_LO(t2 - t0, ST_ASYNC_MS - 150U,
                                    ST_ASYNC_MS + 600U),
             t2 - t0);

    /* 无异步命令时 wait 立即返回 */
    t0 = ros_tick_get();
    rc = rs_cmd_dispatch(s_rs, "wait");
    t1 = ros_tick_get();
    st_check("wait idle non-blocking", (rc == RS_OK) && ((t1 - t0) <= 20U),
             t1 - t0);
}

/* 2) sleep 同步长命令 */
static void st_test_sync(void)
{
    uint32_t t0 = ros_tick_get();
    int rc = rs_cmd_dispatch(s_rs, "sleep 300");
    uint32_t dt = ros_tick_get() - t0;

    st_check("sync sleep duration",
             (rc == RS_OK) && ST_LO(dt, ST_SYNC_MS - 50U, ST_SYNC_MS + 200U),
             dt);
}

/* 3) pause/resume：暂停期间不消耗 sleep 时间 */
static void st_test_pause_resume(void)
{
    ros_task_t *w;
    uint32_t t0;
    uint32_t tp;
    uint32_t tr;
    uint32_t total;
    uint32_t remain;
    uint32_t tick0;
    uint32_t tickp;
    uint32_t tickr;
    uint32_t guard;
    int rc_pause;
    int rc_resume;

    s_wt_done = false;
    s_wt_started = false;
    s_wt_end = 0U;
    w = ros_task_create_dynamic("sh_wt", 2U, st_worker_entry, NULL, 2048U);
    if (w == NULL) {
        s_fail++;
        LOG_E("sht", "FAIL worker create");
        return;
    }

    /* 等 worker 真正开始执行 sleep，避免 pause 早于命令登记 */
    guard = ros_tick_get() + 1000U;
    while (!s_wt_started && ROS_TICK_AFTER(guard, ros_tick_get())) {
        ros_sleep_ms(2U);
    }
    if (!s_wt_started) {
        s_fail++;
        LOG_E("sht", "FAIL worker not scheduled: state=%u", (unsigned)w->state);
        (void)ros_task_destroy(w);
        return;
    }

    tick0 = ros_tick_get();
    t0 = tick0;
    ros_sleep_ms(ST_PAUSE_AT);
    tickp = ros_tick_get();
    tp = tickp;
    rc_pause = rs_cmd_dispatch(s_rs, "pause sh_wt");
    LOG_I("sht", "pause rc=%d at %u ms", rc_pause, (unsigned)(tickp - t0));
    ros_sleep_ms(ST_PAUSED_MS);
    tickr = ros_tick_get();
    tr = tickr;
    rc_resume = rs_cmd_dispatch(s_rs, "resume sh_wt");
    LOG_I("sht", "resume rc=%d at %u ms", rc_resume, (unsigned)(tickr - t0));

    guard = t0 + 5000U;
    while (!s_wt_done && ROS_TICK_AFTER(guard, ros_tick_get())) {
        ros_sleep_ms(10U);
    }
    if (!s_wt_done) {
        s_fail++;
        LOG_E("sht", "FAIL worker stuck: state=%u end=%u",
              (unsigned)w->state, (unsigned)s_wt_end);
        (void)ros_task_destroy(w);
        return;
    }
    /* 等任务真正退出后再回收 TCB/栈 */
    guard = ros_tick_get() + 200U;
    while (w->state != ROS_TASK_TERMINATED && ROS_TICK_AFTER(guard, ros_tick_get())) {
        ros_sleep_ms(1U);
    }
    (void)ros_task_destroy(w);

    total = s_wt_end - t0;
    remain = s_wt_end - tr;

    st_check("pause accepted",
             (rc_pause == 0) && (tickp - tick0 >= ST_PAUSE_AT - 20U),
             tickp - tick0);
    st_check("resume accepted", (rc_resume == 0) && (tickr - tickp >= ST_PAUSED_MS - 20U),
             tickr - tickp);
    /* 总时长 ≈ 900 + 500 = 1400ms（暂停不消耗） */
    st_check("pause does not consume sleep",
             ST_LO(total, ST_WORK_MS + ST_PAUSED_MS - 250U,
                   ST_WORK_MS + ST_PAUSED_MS + 500U),
             total);
    /* 恢复后剩余 ≈ 900 - 300 = 600ms */
    st_check("resume waits remaining",
             ST_LO(remain, ST_WORK_MS - ST_PAUSE_AT - 200U,
                   ST_WORK_MS - ST_PAUSE_AT + 350U),
             remain);
    (void)tp;
    (void)tr;
}

/* ------------------------------ 入口 ------------------------------ */

void app_shelltest_run(void)
{
    static uint8_t done;

    if (done != 0U) {
        return;
    }
    done = 1U;

    LOG_I("sht", "shell lifecycle test start");

    s_rs = rs_create();
    if (s_rs == NULL) {
        s_fail++;
        LOG_E("sht", "FAIL roshell create");
        return;
    }

    st_test_async();
    st_test_sync();
    st_test_pause_resume();

    rs_destroy(s_rs);
    s_rs = NULL;

    if (s_fail == 0U) {
        LOG_I("sht", "shell lifecycle test PASS");
    } else {
        LOG_E("sht", "shell lifecycle test FAIL (%u)", (unsigned)s_fail);
    }
}
