/*
 * roshell_repl.c - 交互式会话外壳：环形缓冲 + 输入任务 + 会话生命周期
 *
 * 行编辑与命令分派不在本文件：字节统一交给 roshell_io 的 rs_repl_feed()，
 * 保证 "任务轮询 / 中断投喂 / 流式读取" 三条入口的行行为完全一致。
 */

#include "roshell_repl.h"
#include "roshell_io.h"
#include "roshell_cmd.h"
#include "ros.h"

#include <string.h>

#define RS_SHELL_TASK_NAME     "roshell_io"

uint32_t rs_shell_process(roshell_t *rs)
{
    uint8_t buf[RS_SHELL_RX_MAX];
    uint32_t n;
    uint32_t lines = 0U;

    if (rs == NULL || !rs->started) {
        return 0U;
    }
    /* 推进本实例的异步持续命令（'-' 启动），保证即使没有输入也在轮转 on_tick */
    rs_cmd_tick_all(rs);
    n = ringbuf_read(&rs->rx, buf, (uint32_t)sizeof(buf));
    if (n > 0U) {
        lines = rs_repl_feed(rs, (const char *)buf, n);
    }
    if (rs->exit_req) {
        rs->finished = true;        /* 输入任务据此退出 */
    }
    return lines;
}

void rs_shell_feed(roshell_t *rs, const char *data, uint32_t len)
{
    if (rs == NULL || !rs->started || data == NULL || len == 0U) {
        return;
    }
    (void)ringbuf_write(&rs->rx, data, len);
}

static void rs_shell_task_entry(void *arg)
{
    roshell_t *rs = (roshell_t *)arg;

    for (;;) {
        (void)rs_shell_process(rs);
        if (rs->finished) {
            break;
        }
        if (ringbuf_available(&rs->rx) == 0U) {
            /* 有异步持续命令时提高轮转频率，保证 sleep 等命令的精度 */
            ros_sleep_ms(rs_cmd_has_async(rs) ? 1U : 5U);
        }
    }
    ros_task_exit();
}

roshell_t *rs_shell_start(const char *version)
{
    roshell_t *rs;

    rs = rs_create();
    if (rs == NULL) {
        ros_printf("shell: out of memory\r\n");
        return NULL;
    }
    if (ringbuf_init(&rs->rx, rs->rx_mem, sizeof(rs->rx_mem)) != 0) {
        rs_destroy(rs);
        return NULL;
    }
    if (rs_repl_open(rs, NULL, NULL, version) != RS_OK) {
        rs_destroy(rs);
        return NULL;
    }
    rs->task = ros_task_create_dynamic(RS_SHELL_TASK_NAME,
                                       RS_SHELL_PRIO,
                                       rs_shell_task_entry, rs,
                                       RS_SHELL_STACK);
    if (rs->task == NULL) {
        ros_mem_info_t info;

        (void)memset(&info, 0, sizeof(info));
        (void)ros_mem_get_info(&info);
        ros_printf("shell: task create failed (stack=%u, free=%u)\r\n",
                   (unsigned)RS_SHELL_STACK, (unsigned)info.free_bytes);
        rs_destroy(rs);
        return NULL;
    }
    return rs;
}

bool rs_shell_started(const roshell_t *rs)
{
    return (rs != NULL) && rs->started;
}

/* 当前工作目录；rs 为空或会话未启动时视为根目录 */
const char *rs_shell_cwd(const roshell_t *rs)
{
    return ((rs != NULL) && (rs->cwd[0] != '\0')) ? rs->cwd : "/";
}

/* 保存当前工作目录（调用者需先校验目录存在） */
int rs_shell_cwd_set(roshell_t *rs, const char *path)
{
    size_t len;

    if (rs == NULL || path == NULL || path[0] != '/') {
        return -1;
    }
    len = strlen(path);
    if (len + 1U > sizeof(rs->cwd)) {
        return -1;
    }
    memcpy(rs->cwd, path, len + 1U);
    return 0;
}

bool rs_shell_task_finished(const roshell_t *rs)
{
    return (rs != NULL) && rs->finished;
}

void rs_shell_request_exit(roshell_t *rs)
{
    if (rs != NULL) {
        rs->exit_req = true;
    }
}

int rs_shell_shutdown(roshell_t *rs)
{
    if (rs == NULL || !rs->started || !rs->finished) {
        return -1;
    }
    if (rs->task != NULL &&
        rs->task != ros_current_task) {     /* 任务内退出时不销毁自身 */
        (void)ros_task_destroy(rs->task);
    }
    rs_destroy(rs);
    return 0;
}

uint32_t rs_shell_line_pending(const roshell_t *rs)
{
    return rs_repl_line_pending(rs);
}
