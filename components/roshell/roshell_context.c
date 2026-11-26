/*
 * roshell_context.c - roshell 会话上下文
 *
 * 接收环形缓冲（feed）、简化行编辑、输入任务（process）、三级分派
 * （内建命令 -> rs_cmd 全局命令表 -> 脚本引擎）与会话生命周期
 * （start / shutdown）。
 */

#include "roshell.h"
#include "roshell_core.h"
#include "roshell_cmd_internal.h"
#include "roshell_context.h"
#include "ros.h"
#include "common.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------ 会话配置 ------------------------------ */
#ifndef RS_SHELL_PRIO
#define RS_SHELL_PRIO          4U      /* 输入任务优先级（0 最高） */
#endif
#ifndef RS_SHELL_STACK
#if defined(ROS_STM32_F1)
#define RS_SHELL_STACK         2048U   /* F1 仅 10KB 堆：命令处理（http/mqtt）需要该栈深度 */
#else
#define RS_SHELL_STACK         4096U   /* 输入任务栈字节数（ls 递归 + 嵌套扫描需要余量） */
#endif
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
    (void)str_vsnprintf(msg, sizeof(msg), fmt, ap);
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

/* 会话状态直接存放在实例中（见 roshell_core.h），多会话即多实例 */

ros_task_t *rs_session_task(const roshell_t *rs)
{
    return (rs != NULL) ? rs->task : NULL;
}

const char *rs_session_version(const roshell_t *rs)
{
    return (rs != NULL) ? rs->version : NULL;
}

/* ============================== 引擎命令 hook ============================== */

/*
 * 脚本引擎的命令 hook（规格 §8 statement -> command）：
 * 只分派命令名行（内建命令表 -> cmd 组件），不进入脚本引擎的语句解释，
 * 避免与引擎递归。返回 1 = 已处理（*ret 为命令返回值）；0 = 不是命令。
 */
static int rs_cmd_hook(void *ctx, const char *line, int *ret)
{
    roshell_t *rs = (roshell_t *)ctx;
    char buf[RS_LINE_MAX];
    char *argv[RS_CMD_ARGC_MAX];
    const rs_cmd_t *cmd;
    bool bg = false;
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
    /* '-' 前缀：持续命令不等待结束（异步启动） */
    if (argv[0][0] == '-' && argv[0][1] != '\0') {
        bg = true;
        argv[0]++;
    }
    cmd = rs_cmd_find_command(argv[0]);
    if (cmd != NULL) {
        if (rs_builtin_is(cmd)) {
            rs_cmd_ctx_t cctx;

            cctx.rs = rs;
            cctx.argc = argc;
            cctx.argv = argv;
            cctx.parsed = NULL;
            cctx.user = cmd->user;
            rc = rs_cmd_execute(cmd, &cctx, !bg);
        } else {
            rc = rs_cmd_exec_node(cmd, argc - 1, &argv[1], NULL, !bg);
        }
        if (bg && rc == RS_CMD_RUNNING) {
            rc = 0;                     /* 已后台启动 */
        }
        *ret = rc;
        return 1;
    }
    if (bg) {
        return 0;                       /* 不是命令：按脚本一元负号处理 */
    }
    rc = rs_builtin_dispatch(argc, argv);
    if (rc != RS_ARGS_ERR_UNKNOWN) {
        *ret = rc;
        return 1;
    }
    return 0;
}

void rs_cmd_install(roshell_t *rs)
{
    if (rs != NULL) {
        rs_set_cmd_hook(rs, rs_cmd_hook, rs);
    }
}

/* ============================== 命令分派 ============================== */

int rs_cmd_dispatch(roshell_t *rs, const char *line)
{
    char buf[RS_LINE_MAX];
    char *argv[RS_CMD_ARGC_MAX];
    rs_err_t err;
    const rs_cmd_t *cmd;
    bool bg = false;
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

    /* '-' 前缀：持续命令不等待结束（异步启动），继续执行下一条命令 */
    if (argv[0][0] == '-' && argv[0][1] != '\0') {
        bg = true;
        argv[0]++;
    }
    cmd = rs_cmd_find_command(argv[0]);
    if (cmd != NULL) {
        if (rs_builtin_is(cmd)) {
            rs_cmd_ctx_t cctx;

            cctx.rs = rs;
            cctx.argc = argc;
            cctx.argv = argv;
            cctx.parsed = NULL;
            cctx.user = cmd->user;
            rc = rs_cmd_execute(cmd, &cctx, !bg);
        } else {
            rc = rs_cmd_exec_node(cmd, argc - 1, &argv[1], NULL, !bg);
            if (rc < 0) {
                rs_error(rc, "command '%s' failed", argv[0]);
            }
        }
        if (bg && rc == RS_CMD_RUNNING) {
            return RS_OK;                   /* 已后台启动 */
        }
        return rc;
    }
    if (bg) {
        /* 不是命令：按脚本表达式（一元负号）处理，落到下方脚本路径 */
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
static bool rs_edit_byte(roshell_t *rs, uint8_t c)
{
    if (c == 0x03U) {                       /* Ctrl-C：清行 */
        rs->line_len = 0U;
        rs->last_cr = false;
        rs->line[0] = '\0';
        ros_puts("^C");
        rs_prompt();
        return false;
    }
    if (c == '\r' || c == '\n') {
        /* 终端可能只发 CR、只发 LF 或发 CRLF：都按一次提交处理 */
        if (c == '\n' && rs->last_cr) {
            rs->last_cr = false;
            return false;
        }
        rs->last_cr = (c == '\r');
        ros_io_write(NULL, "\n", 1U);
        rs->line[rs->line_len] = '\0';
        if (rs->line_len > 0U) {
            (void)rs_cmd_dispatch(rs, rs->line);
        }
        rs->line_len = 0U;
        if (!rs->exit_req) {
            rs_prompt();
        }
        return true;
    }
    rs->last_cr = false;
    if (c == 0x08U || c == 0x7FU) {         /* 退格 */
        if (rs->line_len > 0U) {
            rs->line_len--;
            rs->line[rs->line_len] = '\0';
            ros_io_write(NULL, "\b \b", 3U);
        }
        return false;
    }
    if (c == 0x1BU) {                       /* ESC：吞掉方向键等控制序列 */
        rs->last_esc = true;
        return false;
    }
    if (rs->last_esc) {
        if (c == '[' || (c >= '0' && c <= '9') || c == ';') {
            return false;
        }
        rs->last_esc = false;
        return false;
    }
    if (c >= 0x20U && c < 0x7FU) {
        if (rs->line_len + 1U < RS_LINE_MAX) {   /* 超长行截断 */
            rs->line[rs->line_len++] = (char)c;
            rs->line[rs->line_len] = '\0';
            ros_putchar((char)c);
        }
    }
    return false;
}

/* ============================== 会话 API ============================== */

uint32_t rs_shell_process(roshell_t *rs)
{
    uint32_t lines = 0U;
    uint8_t c;

    if (rs == NULL || !rs->started) {
        return 0U;
    }
    /* 推进异步持续命令（'-' 启动），保证即使没有输入也在轮转 on_tick */
    rs_cmd_tick_all();
    /* 先排空本轮全部输入（含 EOF 前已到达的行），再响应退出请求 */
    while (ringbuf_read(&rs->rx, &c, 1U) == 1U) {
        if (rs_edit_byte(rs, c)) {
            lines++;
        }
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
            ros_sleep_ms(rs_cmd_has_async() ? 1U : 5U);
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
    rs_cmd_install(rs);              /* 块内命令路由（规格 §8） */
    (void)ringbuf_init(&rs->rx, rs->rx_mem, sizeof(rs->rx_mem));
    rs->line_len = 0U;
    rs->line[0] = '\0';
    rs->last_cr = false;
    rs->last_esc = false;
    rs->finished = false;
    rs->exit_req = false;
    rs->version = version;
    rs->cwd[0] = '/';
    rs->cwd[1] = '\0';

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
    rs->started = true;
    ros_printf("roshell ready (%s): type 'help'\n",
               version != NULL ? version : "pc");
    rs_prompt();
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
    if (rs->task != NULL) {
        (void)ros_task_destroy(rs->task);
    }
    rs_destroy(rs);
    return 0;
}

uint32_t rs_shell_line_pending(const roshell_t *rs)
{
    return (rs != NULL) ? rs->line_len : 0U;
}

