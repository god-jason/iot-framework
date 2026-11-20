/*
 * roshell_cmd.c - roshell 命令集与交互会话
 *
 * 命令风格参考旧 shell_cmd.c：静态命令表 + 按行首 token 分派；
 * 关键字 / 命令名一律小写、大小写敏感（roshell.md §1）。
 * 会话部分：接收环形缓冲（feed）+ 简化行编辑 + 输入任务（process）。
 */

#include "roshell_cmd.h"
#include "ros.h"
#include "common.h"
#include "cmd.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------ 会话配置 ------------------------------ */
#ifndef ROSHELL_SHELL_RX_MAX
#define ROSHELL_SHELL_RX_MAX        256U    /* 接收环形缓冲字节数 */
#endif
#ifndef ROSHELL_SHELL_PRIO
#define ROSHELL_SHELL_PRIO          4U      /* 输入任务优先级（0 最高） */
#endif
#ifndef ROSHELL_SHELL_STACK
#define ROSHELL_SHELL_STACK         3072U   /* 输入任务栈字节数 */
#endif
#ifndef ROSHELL_SHELL_WAITJOB_MS
#define ROSHELL_SHELL_WAITJOB_MS    30000U  /* waitjob 默认超时 */
#endif

#define ROSHELL_CMD_ARGC_MAX        16U     /* 单行最多 token 数 */
#define ROSHELL_SHELL_TASK_NAME     "roshell_io"

/* ============================== 小工具 ============================== */

static void ro_prompt(void)
{
    ros_io_write(NULL, "> ", 2U);
}

static void ro_error(int code, const char *fmt, ...)
{
    char msg[64];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    ros_printf("ERR %d: %s\n", code, msg);
}

/* 浮点转文本：不依赖 printf 的 %f（nano 库可能不带浮点格式化） */
static void ro_fmt_float(char *buf, uint32_t cap, float f)
{
    bool neg = (f < 0.0f);
    double d = neg ? (double)(-f) : (double)f;
    long ip = (long)d;
    uint32_t fp = (uint32_t)(((d - (double)ip) * 100.0) + 0.5);

    if (fp >= 100U) {
        fp -= 100U;
        ip++;
    }
    (void)snprintf(buf, cap, "%s%ld.%02u", neg ? "-" : "", ip, (unsigned)fp);
}

/* 行首 token 切分：空白分隔，双引号成对时视为单 token（保留引号内容） */
static int ro_tokenize(char *buf, char **argv, int max)
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

/* 去掉 token 包裹的双引号（echo 用） */
static const char *ro_unquote(const char *tok, uint16_t *len)
{
    if (*len >= 2U && tok[0] == '"' && tok[*len - 1U] == '"') {
        tok++;
        *len = (uint16_t)(*len - 2U);
    }
    return tok;
}

/* 读顶层变量并格式化为文本；找到返回 true */
static bool ro_var_text(roshell_t *rs, const char *name, uint16_t nlen,
                        char *buf, uint32_t cap)
{
    char nm[ROSHELL_VAR_NAME_MAX];
    int32_t i;
    float f;
    bool b;

    buf[0] = '\0';
    if (rs == NULL || nlen == 0U || nlen >= sizeof(nm)) {
        return false;
    }
    memcpy(nm, name, nlen);
    nm[nlen] = '\0';
    if (roshell_get_int(rs, nm, &i) == RO_OK) {
        (void)snprintf(buf, cap, "%ld", (long)i);
        return true;
    }
    if (roshell_get_float(rs, nm, &f) == RO_OK) {
        ro_fmt_float(buf, cap, f);
        return true;
    }
    if (roshell_get_bool(rs, nm, &b) == RO_OK) {
        (void)str_copy(buf, cap, b ? "true" : "false");
        return true;
    }
    return (roshell_get_str(rs, nm, buf, cap) == RO_OK);
}

/* 解析 "#tid" 形式的任务号 */
static bool ro_parse_tid(const char *s, uint16_t *tid)
{
    uint32_t v = 0U;

    if (s == NULL || s[0] != '#') {
        return false;
    }
    s++;
    if (*s == '\0') {
        return false;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return false;
        }
        v = v * 10U + (uint32_t)(*s - '0');
        s++;
    }
    *tid = (uint16_t)v;
    return true;
}

/* 按 #tid 或任务名（大小写敏感）查找任务 */
static ros_task_t *ro_find_task(const char *arg)
{
    ros_task_t *task;
    uint16_t tid = 0U;

    if (arg == NULL) {
        return NULL;
    }
    if (ro_parse_tid(arg, &tid)) {
        for (task = ros_task_first(); task != NULL; task = ros_task_next(task)) {
            if (task->tid == tid) {
                return task;
            }
        }
        return NULL;
    }
    for (task = ros_task_first(); task != NULL; task = ros_task_next(task)) {
        if (task->name != NULL && strcmp(task->name, arg) == 0) {
            return task;
        }
    }
    return NULL;
}

static const char *ro_task_state_name(ros_task_state_e state)
{
    switch (state) {
    case ROS_TASK_READY:      return "READY";
    case ROS_TASK_RUNNING:    return "RUNNING";
    case ROS_TASK_BLOCKED:
    case ROS_TASK_DELAYED:    return "BLOCKED";
    case ROS_TASK_SUSPENDED:  return "SUSPENDED";
    case ROS_TASK_TERMINATED: return "DELETED";
    default:                  return "?";
    }
}

static const char *ro_run_state_name(int state)
{
    switch (state) {
    case RO_RUN_RUNNING: return "RUNNING";
    case RO_RUN_DONE:    return "DONE";
    case RO_RUN_ERROR:   return "ERROR";
    default:             return "FREE";
    }
}

/* ============================== 会话状态 ============================== */

static roshell_t *s_rs;
static ros_task_t *s_task;
static const char *s_version;
static ringbuf_t s_rx;
static uint8_t s_rx_mem[ROSHELL_SHELL_RX_MAX];
static char s_line[ROSHELL_LINE_MAX];
static uint32_t s_line_len;
static bool s_last_cr;
static bool s_last_esc;
static volatile bool s_started;
static volatile bool s_finished;
static volatile bool s_exit_req;

/* ============================== 内建命令 ============================== */

static int ro_cmd_help(roshell_t *rs, int argc, char **argv);

static int cmd_ver(roshell_t *rs, int argc, char **argv)
{
    char info[512];
    int n;

    (void)rs;
    (void)argc;
    (void)argv;
    ros_printf("roshell v1.0\n");
    ros_printf("Build: %s\n", s_version != NULL ? s_version : "-");
    n = ros_version_format(info, sizeof(info));
    if (n > 0) {
        (void)ros_io_write(NULL, info, (uint32_t)n);
    }
    ros_printf("LINE=%u SCRIPT=%u RUN=%u FS=%d\n",
               (unsigned)ROSHELL_LINE_MAX, (unsigned)ROSHELL_SCRIPT_MAX,
               (unsigned)ROSHELL_RUN_MAX, (int)ROSHELL_HAS_FS);
    return 0;
}

static int cmd_echo(roshell_t *rs, int argc, char **argv)
{
    int a;
    bool first = true;

    for (a = 1; a < argc; a++) {
        const char *tok = argv[a];
        uint16_t len = (uint16_t)strlen(tok);
        uint16_t i = 0U;

        tok = ro_unquote(tok, &len);
        if (!first) {
            ros_putchar(' ');
        }
        first = false;

        while (i < len) {
            if (tok[i] == '$' && i + 1U < len) {
                uint16_t start = (uint16_t)(i + 1U);
                uint16_t end = start;
                char buf[64];

                while (end < len && ((tok[end] >= 'a' && tok[end] <= 'z') ||
                                     (tok[end] >= 'A' && tok[end] <= 'Z') ||
                                     (tok[end] >= '0' && tok[end] <= '9') ||
                                     tok[end] == '_')) {
                    end++;
                }
                if (end == start) {
                    ros_putchar('$');
                    i++;
                    continue;
                }
                if (ro_var_text(rs, &tok[start], (uint16_t)(end - start),
                                buf, sizeof(buf))) {
                    (void)ros_io_write(NULL, buf, (uint32_t)strlen(buf));
                } else {
                    ros_printf("WARN: undefined $%.*s\n",
                               (int)(end - start), &tok[start]);
                    (void)ros_io_write(NULL, &tok[start - 1U],
                                       (uint32_t)(end - start + 1U));
                }
                i = end;
                continue;
            }
            ros_putchar(tok[i]);
            i++;
        }
    }
    ros_putchar('\n');
    return 0;
}

static void ro_ps_run_slots(roshell_t *rs)
{
    uint32_t i;
    bool any = false;

    ros_printf("run slots:\n");
    if (rs == NULL) {
        ros_printf("  (no instance)\n");
        return;
    }
    for (i = 0U; i < ROSHELL_RUN_MAX; i++) {
        int st = roshell_run_state(rs, (int)i);

        if (st != RO_RUN_FREE) {
            any = true;
            ros_printf("  %2u  %s\n", (unsigned)i, ro_run_state_name(st));
        }
    }
    if (!any) {
        ros_printf("  (none)\n");
    }
}

static int cmd_ps(roshell_t *rs, int argc, char **argv)
{
    ros_task_t *task;

    (void)argc;
    (void)argv;
    ros_printf("TID  NAME          PRIO  STATE      STACK   CPU%%\n");
    ros_printf("---  ------------  ----  ---------  ------  -----\n");
    for (task = ros_task_first(); task != NULL; task = ros_task_next(task)) {
        char cpu[8] = "-";
        char stack[8];

        if (task->state == ROS_TASK_TERMINATED) {
            continue;
        }
        (void)snprintf(stack, sizeof(stack), "%u", (unsigned)task->stack_size);
        {
            uint32_t pm = ros_task_cpu_permille(task);

            (void)snprintf(cpu, sizeof(cpu), "%u.%u", (unsigned)(pm / 10U),
                           (unsigned)(pm % 10U));
        }
        ros_printf("%3u  %-13s %4u  %-10s %6s  %5s\n",
                   (unsigned)task->tid,
                   task->name != NULL ? task->name : "?",
                   (unsigned)task->priority,
                   ro_task_state_name(task->state), stack, cpu);
    }
    ro_ps_run_slots(rs);
    return 0;
}

static int cmd_jobs(roshell_t *rs, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    (void)rs;
    ro_ps_run_slots(rs);
    return 0;
}

/* waitjob 等待期间消费输入：只识别 Ctrl-C，其余字节丢弃 */
static bool ro_pump_ctrlc(void)
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

static int cmd_waitjob(roshell_t *rs, int argc, char **argv)
{
    int32_t slot = 0;
    uint32_t timeout = ROSHELL_SHELL_WAITJOB_MS;
    uint32_t waited = 0U;
    int rc;
    int i;

    if (argc < 2 || !str_parse_int(argv[1], &slot)) {
        ro_error(RO_ERR_PARAM, "usage: waitjob <n> [timeout <ms>]");
        return -RO_ERR_PARAM;
    }
    for (i = 2; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "timeout") == 0) {
            uint32_t t;

            if (str_parse_uint(argv[i + 1], &t)) {
                timeout = t;
            }
        }
    }
    if (rs == NULL) {
        ro_error(RO_ERR_PARAM, "no roshell instance");
        return -RO_ERR_PARAM;
    }
    for (;;) {
        rc = roshell_run_wait(rs, (int)slot, 100U);
        if (rc != RO_ERR_TIMEOUT) {
            break;
        }
        if (ro_pump_ctrlc()) {
            ros_puts("WARN: wait interrupted");
            return 0;
        }
        waited += 100U;
        if (timeout != 0U && waited >= timeout) {
            ro_error(RO_ERR_TIMEOUT, "waitjob %d timeout", (int)slot);
            return -RO_ERR_TIMEOUT;
        }
    }
    if (rc == RO_OK) {
        return 0;
    }
    if (rc == RO_ERR_PARAM) {
        ro_error(RO_ERR_NOT_FOUND, "slot %d not found", (int)slot);
        return -RO_ERR_NOT_FOUND;
    }
    ro_error(rc, "run %d failed", (int)slot);
    return rc;
}

/* 会话任务 / roshell run 任务 / idle / 自身不允许 kill */
static bool ro_task_protected(const ros_task_t *task)
{
    return (task == ros_current_task) || (task == ros_idle_task) ||
           (task == s_task) ||
           (task->name != NULL && strcmp(task->name, "roshell") == 0);
}

static int cmd_kill(roshell_t *rs, int argc, char **argv)
{
    ros_task_t *task;

    (void)rs;
    if (argc < 2) {
        ro_error(RO_ERR_PARAM, "usage: kill <name|#tid>");
        return -RO_ERR_PARAM;
    }
    task = ro_find_task(argv[1]);
    if (task == NULL) {
        ro_error(RO_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -RO_ERR_NOT_FOUND;
    }
    if (ro_task_protected(task)) {
        ro_error(RO_ERR_PARAM, "refusing to kill this task");
        return -RO_ERR_PARAM;
    }
    (void)cmd_notify_kill(task);
    if (ros_task_destroy(task) != ROS_OK) {
        ro_error(RO_ERR_PARAM, "task busy or static");
        return -RO_ERR_PARAM;
    }
    ros_printf("task %s deleted\n", argv[1]);
    return 0;
}

static int cmd_pause(roshell_t *rs, int argc, char **argv)
{
    ros_task_t *task;

    (void)rs;
    if (argc < 2) {
        ro_error(RO_ERR_PARAM, "usage: pause <name|#tid>");
        return -RO_ERR_PARAM;
    }
    task = ro_find_task(argv[1]);
    if (task == NULL) {
        ro_error(RO_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -RO_ERR_NOT_FOUND;
    }
    if (task == ros_current_task || task == ros_idle_task || task == s_task) {
        ro_error(RO_ERR_PARAM, "refusing to pause this task");
        return -RO_ERR_PARAM;
    }
    if (cmd_notify_pause(task) < 0) {
        ro_error(RO_ERR_PARAM, "command refused pause");
        return -RO_ERR_PARAM;
    }
    if (ros_task_suspend(task) != ROS_OK) {
        (void)cmd_notify_resume(task);
        ro_error(RO_ERR_PARAM, "task cannot be paused");
        return -RO_ERR_PARAM;
    }
    return 0;
}

static int cmd_resume(roshell_t *rs, int argc, char **argv)
{
    ros_task_t *task;

    (void)rs;
    if (argc < 2) {
        ro_error(RO_ERR_PARAM, "usage: resume <name|#tid>");
        return -RO_ERR_PARAM;
    }
    task = ro_find_task(argv[1]);
    if (task == NULL) {
        ro_error(RO_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -RO_ERR_NOT_FOUND;
    }
    if (ros_task_resume(task) != ROS_OK) {
        ro_error(RO_ERR_PARAM, "task is not suspended");
        return -RO_ERR_PARAM;
    }
    (void)cmd_notify_resume(task);
    return 0;
}

static void ro_mem_line(void)
{
    ros_mem_info_t mem;

    (void)memset(&mem, 0, sizeof(mem));
    if (ros_mem_get_info(&mem) != 0) {
        ros_printf("heap      : n/a\n");
        return;
    }
    if (mem.total_bytes > 0U) {
        ros_printf("heap      : %u/%u B, free %u B (max block %u B)\n",
                   (unsigned)mem.used_bytes, (unsigned)mem.total_bytes,
                   (unsigned)mem.free_bytes, (unsigned)mem.max_free_bytes);
    } else {
        ros_printf("heap      : %u B used\n", (unsigned)mem.used_bytes);
    }
    if (mem.untracked_bytes > 0U) {
        ros_printf("heap-sys  : %u B (no task context)\n",
                   (unsigned)mem.untracked_bytes);
    }
}

static int cmd_mem(roshell_t *rs, int argc, char **argv)
{
    (void)rs;
    (void)argc;
    (void)argv;
    ro_mem_line();
    return 0;
}

static int cmd_state(roshell_t *rs, int argc, char **argv)
{
    uint32_t ready = 0U;
    uint32_t running = 0U;
    uint32_t blocked = 0U;
    uint32_t suspended = 0U;
    uint32_t total = 0U;
    uint32_t run_busy = 0U;
    ros_stats_t sys;
    ros_task_t *task;
    uint32_t idle_pm;
    uint32_t busy_pm;
    uint32_t i;

    (void)argc;
    (void)argv;

    for (task = ros_task_first(); task != NULL; task = ros_task_next(task)) {
        if (task->state == ROS_TASK_TERMINATED) {
            continue;
        }
        total++;
        switch (task->state) {
        case ROS_TASK_READY:     ready++; break;
        case ROS_TASK_RUNNING:   running++; break;
        case ROS_TASK_BLOCKED:
        case ROS_TASK_DELAYED:   blocked++; break;
        case ROS_TASK_SUSPENDED: suspended++; break;
        default: break;
        }
    }
    ros_printf("tasks     : %u (RUN=%u READY=%u BLOCK=%u SUSP=%u)\n",
               (unsigned)total, (unsigned)running, (unsigned)ready,
               (unsigned)blocked, (unsigned)suspended);

    (void)memset(&sys, 0, sizeof(sys));
    (void)ros_stats_get(&sys);
    idle_pm = (ros_idle_task != NULL) ? ros_task_cpu_permille(ros_idle_task)
                                      : 0U;
    busy_pm = (idle_pm <= 1000U) ? (1000U - idle_pm) : 0U;
    ros_printf("cpu       : %u.%u%% busy, uptime=%us, ctx=%u\n",
               (unsigned)(busy_pm / 10U), (unsigned)(busy_pm % 10U),
               (unsigned)(sys.tick_count / 1000U),
               (unsigned)sys.ctx_switches);

    ro_mem_line();

    if (rs != NULL) {
        for (i = 0U; i < ROSHELL_RUN_MAX; i++) {
            if (roshell_run_state(rs, (int)i) == RO_RUN_RUNNING) {
                run_busy++;
            }
        }
    }
    ros_printf("run slots : %u running / %u total\n",
               (unsigned)run_busy, (unsigned)ROSHELL_RUN_MAX);
    return 0;
}

static int cmd_exit(roshell_t *rs, int argc, char **argv)
{
    (void)rs;
    (void)argc;
    (void)argv;
    ros_puts("bye");
    roshell_shell_request_exit();
    return 0;
}

/* ------------------------------ help ------------------------------ */

typedef struct {
    const char *name;
    int (*handler)(roshell_t *rs, int argc, char **argv);
    const char *usage;
    const char *group;
} ro_cmd_t;

static const ro_cmd_t s_cmds[] = {
    { "help",    ro_cmd_help, "help [cmd]",                  "basic" },
    { "ver",     cmd_ver,     "ver",                         "basic" },
    { "echo",    cmd_echo,    "echo <text|$var>...",         "basic" },
    { "ps",      cmd_ps,      "ps",                          "task"  },
    { "kill",    cmd_kill,    "kill <name|#tid>",            "task"  },
    { "pause",   cmd_pause,   "pause <name|#tid>",           "task"  },
    { "resume",  cmd_resume,  "resume <name|#tid>",          "task"  },
    { "mem",     cmd_mem,     "mem",                         "sys"   },
    { "state",   cmd_state,   "state",                       "sys"   },
    { "jobs",    cmd_jobs,    "jobs",                        "run"   },
    { "waitjob", cmd_waitjob, "waitjob <n> [timeout <ms>]",  "run"   },
    { "exit",    cmd_exit,    "exit",                        "session" }
};

#define RO_CMD_COUNT (sizeof(s_cmds) / sizeof(s_cmds[0]))

static const ro_cmd_t *ro_cmd_find(const char *name)
{
    uint32_t i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0U; i < RO_CMD_COUNT; i++) {
        if (strcmp(s_cmds[i].name, name) == 0) {     /* 大小写敏感 */
            return &s_cmds[i];
        }
    }
    return NULL;
}

static int ro_cmd_help(roshell_t *rs, int argc, char **argv)
{
    uint32_t i;

    (void)rs;
    if (argc >= 2) {
        const ro_cmd_t *cmd = ro_cmd_find(argv[1]);

        if (cmd != NULL) {
            ros_printf("%s\n", cmd->usage);
            return 0;
        }
        {
            const cmd_node_t *node = cmd_find_command(argv[1]);
            char buf[160];

            if (node != NULL && node->def != NULL &&
                cmd_help(node->def, buf, sizeof(buf)) > 0) {
                (void)ros_io_write(NULL, buf, (uint32_t)strlen(buf));
                return 0;
            }
        }
        ro_error(RO_ERR_NOT_FOUND, "no help for '%s'", argv[1]);
        return -RO_ERR_NOT_FOUND;
    }

    ros_printf("Commands:\n");
    for (i = 0U; i < RO_CMD_COUNT; i++) {
        ros_printf("  %-28s %s\n", s_cmds[i].usage, s_cmds[i].group);
    }
    {
        const cmd_node_t *node;

        for (node = cmd_first(); node != NULL; node = cmd_next(node)) {
            char buf[128];

            if (node->def != NULL &&
                cmd_usage(node->def, buf, sizeof(buf)) > 0) {
                ros_printf("  %-28s %s\n", buf,
                           node->def->desc != NULL ? node->def->desc : "");
            }
        }
    }
    ros_puts("script lines (non-command) run on the roshell engine");
    return 0;
}

/* ============================== 命令分派 ============================== */

int roshell_cmd_dispatch(roshell_t *rs, const char *line)
{
    char buf[ROSHELL_LINE_MAX];
    char *argv[ROSHELL_CMD_ARGC_MAX];
    roshell_err_t err;
    const ro_cmd_t *cmd;
    int argc;
    int rc;

    if (line == NULL) {
        return RO_ERR_PARAM;
    }
    (void)str_copy(buf, sizeof(buf), line);
    argc = ro_tokenize(buf, argv, ROSHELL_CMD_ARGC_MAX);
    if (argc <= 0) {
        return RO_OK;                       /* 空行 */
    }

    cmd = ro_cmd_find(argv[0]);
    if (cmd != NULL) {
        return cmd->handler(rs, argc, argv);
    }

    rc = cmd_dispatch(argc, argv);
    if (rc != ARGS_ERR_UNKNOWN) {
        if (rc < 0) {
            ro_error(rc, "command '%s' failed", argv[0]);
        }
        return rc;
    }

    /* 非命令：整行交给脚本引擎 */
    if (rs == NULL) {
        ro_error(RO_ERR_PARAM, "no roshell instance");
        return -RO_ERR_PARAM;
    }
    (void)memset(&err, 0, sizeof(err));
    rc = roshell_exec_line(rs, line, &err);
    if (rc != RO_OK) {
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
static bool ro_edit_byte(uint8_t c)
{
    if (c == 0x03U) {                       /* Ctrl-C：清行 */
        s_line_len = 0U;
        s_last_cr = false;
        s_line[0] = '\0';
        ros_puts("^C");
        ro_prompt();
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
            (void)roshell_cmd_dispatch(s_rs, s_line);
        }
        s_line_len = 0U;
        if (!s_exit_req) {
            ro_prompt();
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
        if (s_line_len + 1U < ROSHELL_LINE_MAX) {   /* 超长行截断 */
            s_line[s_line_len++] = (char)c;
            s_line[s_line_len] = '\0';
            ros_putchar((char)c);
        }
    }
    return false;
}

/* ============================== 会话 API ============================== */

uint32_t roshell_shell_process(void)
{
    uint32_t lines = 0U;
    uint8_t c;

    if (!s_started) {
        return 0U;
    }
    /* 先排空本轮全部输入（含 EOF 前已到达的行），再响应退出请求 */
    while (ringbuf_read(&s_rx, &c, 1U) == 1U) {
        if (ro_edit_byte(c)) {
            lines++;
        }
    }
    if (s_exit_req) {
        s_finished = true;      /* 输入任务据此退出 */
    }
    return lines;
}

void roshell_shell_feed(const char *data, uint32_t len)
{
    if (!s_started || data == NULL || len == 0U) {
        return;
    }
    (void)ringbuf_write(&s_rx, data, len);
}

static void roshell_shell_task_entry(void *arg)
{
    (void)arg;
    for (;;) {
        (void)roshell_shell_process();
        if (s_finished) {
            break;
        }
        if (ringbuf_available(&s_rx) == 0U) {
            ros_sleep_ms(5U);
        }
    }
    ros_task_exit();
}

int roshell_shell_start(const char *version)
{
    if (s_started) {
        return 0;
    }
    s_rs = roshell_create();
    if (s_rs == NULL) {
        return -1;
    }
    (void)ringbuf_init(&s_rx, s_rx_mem, sizeof(s_rx_mem));
    s_line_len = 0U;
    s_line[0] = '\0';
    s_last_cr = false;
    s_last_esc = false;
    s_finished = false;
    s_exit_req = false;
    s_version = version;

    s_task = ros_task_create_dynamic(ROSHELL_SHELL_TASK_NAME,
                                     ROSHELL_SHELL_PRIO,
                                     roshell_shell_task_entry, NULL,
                                     ROSHELL_SHELL_STACK);
    if (s_task == NULL) {
        roshell_destroy(s_rs);
        s_rs = NULL;
        s_version = NULL;
        return -1;
    }
    s_started = true;
    ros_printf("roshell ready (%s): type 'help'\n",
               version != NULL ? version : "pc");
    ro_prompt();
    return 0;
}

bool roshell_shell_started(void)
{
    return s_started;
}

bool roshell_shell_task_finished(void)
{
    return s_finished;
}

void roshell_shell_request_exit(void)
{
    s_exit_req = true;
}

int roshell_shell_shutdown(void)
{
    if (!s_started || !s_finished) {
        return -1;
    }
    if (s_task != NULL) {
        (void)ros_task_destroy(s_task);
        s_task = NULL;
    }
    roshell_destroy(s_rs);
    s_rs = NULL;
    s_version = NULL;
    s_started = false;
    s_exit_req = false;
    s_finished = false;
    s_line_len = 0U;
    ringbuf_reset(&s_rx);
    return 0;
}

roshell_t *roshell_shell_instance(void)
{
    return s_rs;
}

uint32_t roshell_shell_line_pending(void)
{
    return s_line_len;
}
