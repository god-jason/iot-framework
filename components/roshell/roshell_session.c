/*
 * roshell_session.c - roshell 交互会话
 *
 * 接收环形缓冲（feed）、简化行编辑、输入任务（process）、三级分派
 * （内建命令 -> rs_cmd 全局命令表 -> 脚本引擎）与会话生命周期
 * （start / shutdown）。
 */

#include "roshell.h"
#include "roshell_cmd_internal.h"
#include "roshell_session.h"
#include "ros.h"
#include "common.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------ 会话配置 ------------------------------ */
#ifndef RS_SHELL_RX_MAX
#define RS_SHELL_RX_MAX        256U    /* 接收环形缓冲字节数 */
#endif
#ifndef RS_SHELL_PRIO
#define RS_SHELL_PRIO          4U      /* 输入任务优先级（0 最高） */
#endif
#ifndef RS_SHELL_STACK
#define RS_SHELL_STACK         3072U   /* 输入任务栈字节数 */
#endif

#define RS_CMD_ARGC_MAX        16U     /* 单行最多 token 数 */
#define RS_SHELL_TASK_NAME     "roshell_io"

/* ============================== 小工具（会话层与内建命令共用） ============================== */

static void rs_prompt(void)
{
    ros_io_write(NULL, "> ", 2U);
}

void rs_error(int code, const char *fmt, ...)
{
    char msg[64];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    ros_printf("ERR %d: %s\n", code, msg);
}

/* 行首 token 切分：空白分隔，双引号成对时视为单 token（保留引号内容） */
static int rs_tokenize(char *buf, char **argv, int max)
{
    int argc = 0;
    char *p = buf;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0' || argc >= max) {
            break;
        }
        argv[argc++] = p;
        if (*p == '"') {
            p++;
            while (*p != '\0' && *p != '"') {
                p++;
            }
            if (*p == '"') {
                p++;
            }
        } else {
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                p++;
            }
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return argc;
}

/* ============================== 会话状态 ============================== */

static rs_t *s_rs;
static ros_task_t *s_task;
static const char *s_version;
static ringbuf_t s_rx;
static uint8_t s_rx_mem[RS_SHELL_RX_MAX];
static char s_line[RS_LINE_MAX];
static uint32_t s_line_len;
static bool s_last_cr;
static bool s_last_esc;
static volatile bool s_started;
static volatile bool s_finished;
static volatile bool s_exit_req;

/* ------------------------------ 会话状态访问（供内建命令） ------------------------------ */

/* waitjob 等待期间消费输入：只识别 Ctrl-C，其余字节丢弃 */
bool rs_session_pump_ctrlc(void)
{
    bool hit = false;
    uint8_t c;

    while (ringbuf_read(&s_rx, &c, 1U) == 1U) {
        if (c == 0x03U) {
            hit = true;
        }
    }
    return hit;
}

ros_task_t *rs_session_task(void)
{
    return s_task;
}

const char *rs_session_version(void)
{
    return s_version;
}

/* ============================== 引擎命令 hook ============================== */

/*
 * 脚本引擎的命令 hook（规格 §8 statement -> command）：
 * 只分派命令名行（内建命令表 -> cmd 组件），不进入脚本引擎的语句解释，
 * 避免与引擎递归。返回 1 = 已处理（*ret 为命令返回值）；0 = 不是命令。
 */
static int rs_cmd_hook(void *ctx, const char *line, int *ret)
{
    rs_t *rs = (rs_t *)ctx;
    char buf[RS_LINE_MAX];
    char *argv[RS_CMD_ARGC_MAX];
    const rs_cmd_t *cmd;
    int argc;
    int rc;

    if (line == NULL || ret == NULL) {
        return 0;
    }
    (void)str_copy(buf, sizeof(buf), line);
    argc = rs_tokenize(buf, argv, RS_CMD_ARGC_MAX);
    if (argc <= 0) {
        return 0;
    }
    cmd = rs_builtin_find(argv[0]);
    if (cmd != NULL) {
        rs_cmd_ctx_t cctx;
        ros_task_t *self = ros_current_task;

        cctx.rs = rs;
        cctx.argc = argc;
        cctx.argv = argv;
        cctx.parsed = NULL;
        cctx.user = cmd->user;
        /* 内建命令也登记 active，pause/resume/kill 才能找到钩子 */
        rs_cmd_active_push(self, cmd);
        *ret = cmd->fn(&cctx);
        rs_cmd_active_pop(self, cmd);
        return 1;
    }
    rc = rs_builtin_dispatch(argc, argv);
    if (rc != RS_ARGS_ERR_UNKNOWN) {
        *ret = rc;
        return 1;
    }
    return 0;
}

void rs_cmd_install(rs_t *rs)
{
    if (rs != NULL) {
        rs_set_cmd_hook(rs, rs_cmd_hook, rs);
    }
}

/* ============================== 命令分派 ============================== */

int rs_cmd_dispatch(rs_t *rs, const char *line)
{
    char buf[RS_LINE_MAX];
    char *argv[RS_CMD_ARGC_MAX];
    rs_err_t err;
    const rs_cmd_t *cmd;
    int argc;
    int rc;

    if (line == NULL) {
        return RS_ERR_PARAM;
    }
    (void)str_copy(buf, sizeof(buf), line);
    argc = rs_tokenize(buf, argv, RS_CMD_ARGC_MAX);
    if (argc <= 0) {
        return RS_OK;                       /* 空行 */
    }

    cmd = rs_builtin_find(argv[0]);
    if (cmd != NULL) {
        rs_cmd_ctx_t cctx;
        ros_task_t *self = ros_current_task;
        int rc2;

        cctx.rs = rs;
        cctx.argc = argc;
        cctx.argv = argv;
        cctx.parsed = NULL;
        cctx.user = cmd->user;
        /* 内建命令也登记 active，pause/resume/kill 才能找到钩子 */
        rs_cmd_active_push(self, cmd);
        rc2 = cmd->fn(&cctx);
        rs_cmd_active_pop(self, cmd);
        return rc2;
    }

    rc = rs_builtin_dispatch(argc, argv);
    if (rc != RS_ARGS_ERR_UNKNOWN) {
        if (rc < 0) {
            rs_error(rc, "command '%s' failed", argv[0]);
        }
        return rc;
    }

    /* 非命令：整行交给脚本引擎 */
    if (rs == NULL) {
        rs_error(RS_ERR_PARAM, "no roshell instance");
        return -RS_ERR_PARAM;
    }
    (void)memset(&err, 0, sizeof(err));
    rc = rs_exec_line(rs, line, &err);
    if (rc != RS_OK) {
        if (err.msg[0] != '\0') {
            ros_printf("ERR %d (line %u): %s\n", err.code,
                       (unsigned)err.line, err.msg);
        } else {
            ros_printf("ERR %d (line %u)\n", rc, (unsigned)err.line);
        }
    }
    return rc;
}

/* ============================== 行编辑 ============================== */

/* 处理一个输入字节；返回 true 表示提交了一行 */
static bool rs_edit_byte(uint8_t c)
{
    if (c == 0x03U) {                       /* Ctrl-C：清行 */
        s_line_len = 0U;
        s_last_cr = false;
        s_line[0] = '\0';
        ros_puts("^C");
        rs_prompt();
        return false;
    }
    if (c == '\r' || c == '\n') {
        /* 终端可能只发 CR、只发 LF 或发 CRLF：都按一次提交处理 */
        if (c == '\n' && s_last_cr) {
            s_last_cr = false;
            return false;
        }
        s_last_cr = (c == '\r');
        ros_io_write(NULL, "\n", 1U);
        s_line[s_line_len] = '\0';
        if (s_line_len > 0U) {
            (void)rs_cmd_dispatch(s_rs, s_line);
        }
        s_line_len = 0U;
        if (!s_exit_req) {
            rs_prompt();
        }
        return true;
    }
    s_last_cr = false;
    if (c == 0x08U || c == 0x7FU) {         /* 退格 */
        if (s_line_len > 0U) {
            s_line_len--;
            s_line[s_line_len] = '\0';
            ros_io_write(NULL, "\b \b", 3U);
        }
        return false;
    }
    if (c == 0x1BU) {                       /* ESC：吞掉方向键等控制序列 */
        s_last_esc = true;
        return false;
    }
    if (s_last_esc) {
        if (c == '[' || (c >= '0' && c <= '9') || c == ';') {
            return false;
        }
        s_last_esc = false;
        return false;
    }
    if (c >= 0x20U && c < 0x7FU) {
        if (s_line_len + 1U < RS_LINE_MAX) {   /* 超长行截断 */
            s_line[s_line_len++] = (char)c;
            s_line[s_line_len] = '\0';
            ros_putchar((char)c);
        }
    }
    return false;
}

/* ============================== 会话 API ============================== */

uint32_t rs_shell_process(void)
{
    uint32_t lines = 0U;
    uint8_t c;

    if (!s_started) {
        return 0U;
    }
    /* 先排空本轮全部输入（含 EOF 前已到达的行），再响应退出请求 */
    while (ringbuf_read(&s_rx, &c, 1U) == 1U) {
        if (rs_edit_byte(c)) {
            lines++;
        }
    }
    if (s_exit_req) {
        s_finished = true;      /* 输入任务据此退出 */
    }
    return lines;
}

void rs_shell_feed(const char *data, uint32_t len)
{
    if (!s_started || data == NULL || len == 0U) {
        return;
    }
    (void)ringbuf_write(&s_rx, data, len);
}

static void rs_shell_task_entry(void *arg)
{
    (void)arg;
    for (;;) {
        (void)rs_shell_process();
        if (s_finished) {
            break;
        }
        if (ringbuf_available(&s_rx) == 0U) {
            ros_sleep_ms(5U);
        }
    }
    ros_task_exit();
}

int rs_shell_start(const char *version)
{
    if (s_started) {
        return 0;
    }
    s_rs = rs_create();
    if (s_rs == NULL) {
        return -1;
    }
    rs_cmd_install(s_rs);            /* 块内命令路由（规格 §8） */
    (void)ringbuf_init(&s_rx, s_rx_mem, sizeof(s_rx_mem));
    s_line_len = 0U;
    s_line[0] = '\0';
    s_last_cr = false;
    s_last_esc = false;
    s_finished = false;
    s_exit_req = false;
    s_version = version;

    s_task = ros_task_create_dynamic(RS_SHELL_TASK_NAME,
                                     RS_SHELL_PRIO,
                                     rs_shell_task_entry, NULL,
                                     RS_SHELL_STACK);
    if (s_task == NULL) {
        rs_destroy(s_rs);
        s_rs = NULL;
        s_version = NULL;
        return -1;
    }
    s_started = true;
    ros_printf("roshell ready (%s): type 'help'\n",
               version != NULL ? version : "pc");
    rs_prompt();
    return 0;
}

bool rs_shell_started(void)
{
    return s_started;
}

bool rs_shell_task_finished(void)
{
    return s_finished;
}

void rs_shell_request_exit(void)
{
    s_exit_req = true;
}

int rs_shell_shutdown(void)
{
    if (!s_started || !s_finished) {
        return -1;
    }
    if (s_task != NULL) {
        (void)ros_task_destroy(s_task);
        s_task = NULL;
    }
    rs_destroy(s_rs);
    s_rs = NULL;
    s_version = NULL;
    s_started = false;
    s_exit_req = false;
    s_finished = false;
    s_line_len = 0U;
    ringbuf_reset(&s_rx);
    return 0;
}

rs_t *rs_shell_instance(void)
{
    return s_rs;
}

uint32_t rs_shell_line_pending(void)
{
    return s_line_len;
}

