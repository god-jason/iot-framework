/*
 * roshell_builtin.c - roshell 内建命令实现
 *
 * 内建命令静态表（初始化时登记进全局命令表 map）+ 归属判断 + help 输出。
 * 关键字 / 命令名一律小写、大小写敏感（roshell.md §1）。
 * 命令行分派在 roshell_cmd.c，会话与行编辑在 roshell.c；
 * 内置函数 / 常量表在 roshell_func.c。
 */

#include "roshell.h"
#include "roshell_repl.h"
#include "ros.h"
#include "common.h"

#include <string.h>
#include <stdio.h>

/* ============================== 小工具（仅内建命令使用） ============================== */

/* 浮点转文本：不依赖 printf 的 %f（nano 库可能不带浮点格式化） */
static void rs_fmt_float(char *buf, uint32_t cap, float f)
{
    bool neg = (f < 0.0f);
    float v = neg ? -f : f;
    long ip = (long)v;
    uint32_t fp = (uint32_t)(((v - (float)ip) * 100.0f) + 0.5f);

    if (fp >= 100U) {
        fp -= 100U;
        ip++;
    }
    (void)str_printf(buf, cap, "%s%ld.%02u", neg ? "-" : "", ip, (unsigned)fp);
}

/* 去掉 token 包裹的双引号（echo 用） */
static const char *rs_unquote(const char *tok, uint16_t *len)
{
    if (*len >= 2U && tok[0] == '"' && tok[*len - 1U] == '"') {
        tok++;
        *len = (uint16_t)(*len - 2U);
    }
    return tok;
}

/* 读顶层变量并格式化为文本；找到返回 true */
static bool rs_var_text(roshell_t *rs, const char *name, uint16_t nlen,
                        char *buf, uint32_t cap)
{
    char nm[RS_VAR_NAME_MAX];
    int32_t i;
    float f;
    bool b;

    buf[0] = '\0';
    if (rs == NULL || nlen == 0U || nlen >= sizeof(nm)) {
        return false;
    }
    memcpy(nm, name, nlen);
    nm[nlen] = '\0';
    if (rs_get_int(rs, nm, &i) == RS_OK) {
        (void)str_printf(buf, cap, "%ld", (long)i);
        return true;
    }
    if (rs_get_float(rs, nm, &f) == RS_OK) {
        rs_fmt_float(buf, cap, f);
        return true;
    }
    if (rs_get_bool(rs, nm, &b) == RS_OK) {
        (void)str_copy(buf, cap, b ? "true" : "false");
        return true;
    }
    return (rs_get_str(rs, nm, buf, cap) == RS_OK);
}

/* 解析 "#tid" 形式的任务号 */
static bool rs_parse_tid(const char *s, uint16_t *tid)
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
static ros_task_t *rs_find_task(const char *arg)
{
    ros_task_t *task;
    uint16_t tid = 0U;

    if (arg == NULL) {
        return NULL;
    }
    if (rs_parse_tid(arg, &tid)) {
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

static const char *rs_task_state_name(ros_task_state_e state)
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

/* ============================== 内建命令 ============================== */

int rs_cmd_ver(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    char info[512];
    const char *ver;
    int n;

    (void)argc;
    (void)argv;
    (void)parsed;
    (void)user;
    ver = rs_session_version(rs);
    ros_printf("roshell v1.0\n");
    ros_printf("Build: %s\n", ver != NULL ? ver : "-");
    n = ros_version_format(info, sizeof(info));
    if (n > 0) {
        (void)ros_io_write(NULL, info, (uint32_t)n);
    }
    ros_printf("LINE=%u SCRIPT=%u\n",
               (unsigned)RS_LINE_MAX, (unsigned)RS_SCRIPT_MAX);
    return 0;
}

int rs_cmd_echo(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    int a;
    bool first = true;

    (void)parsed;
    (void)user;
    for (a = 1; a < argc; a++) {
        const char *tok = argv[a];
        uint16_t len = (uint16_t)strlen(tok);
        uint16_t i = 0U;

        tok = rs_unquote(tok, &len);
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
                if (rs_var_text(rs, &tok[start], (uint16_t)(end - start),
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

int rs_cmd_ps(roshell_t *rs, int argc, char **argv,
              const rs_args_parsed_t *parsed, void *user)
{
    ros_task_t *task;

    (void)rs;
    (void)argc;
    (void)argv;
    (void)parsed;
    (void)user;
    ros_printf("TID  NAME          PRIO  STATE      STACK   CPU%%\n");
    ros_printf("---  ------------  ----  ---------  ------  -----\n");
    for (task = ros_task_first(); task != NULL; task = ros_task_next(task)) {
        char cpu[8] = "-";
        char stack[8];

        if (task->state == ROS_TASK_TERMINATED) {
            continue;
        }
        (void)str_printf(stack, sizeof(stack), "%u", (unsigned)task->stack_size);
        {
            uint32_t pm = ros_task_cpu_permille(task);

            (void)str_printf(cpu, sizeof(cpu), "%u.%u", (unsigned)(pm / 10U),
                           (unsigned)(pm % 10U));
        }
        ros_printf("%3u  %-13s %4u  %-10s %6s  %5s\n",
                   (unsigned)task->tid,
                   task->name != NULL ? task->name : "?",
                   (unsigned)task->priority,
                   rs_task_state_name(task->state), stack, cpu);
    }
    return 0;
}

/* ---------------- sleep 可暂停状态 ---------------- */
/*
 * sleep <ms> 是持续命令：等待信息登记在会话实例里（rs->sleeps，单表项）。
 *   on_pause  记录剩余时间 remain 并置 paused，tick 期间不推进 deadline；
 *   on_resume 以 now + remain 重设 deadline 并解除停放；
 *   on_tick   到期返回 0 结束；
 *   on_stop   清表项（正常结束或被 kill 统一调用）。
 * pause 命令命中停放（rs_sleep_is_parked）后不再做 OS 级挂起。
 * 同一会话同一时刻只有一个 sleep 等待，回调无需任务身份。
 */

typedef struct {
    uint32_t deadline;      /* 本轮等待截止 tick */
    uint32_t remain;        /* 暂停时记录的剩余毫秒 */
    bool     paused;        /* 停放中：不消耗等待时间 */
    bool     used;          /* 表项是否有效 */
} rs_sleep_t;

/* 会话上下文的 sleep 登记表（vec 位于 struct roshell，首次使用时预分配） */
static vec_t *rs_sleep_vec(roshell_t *rs)
{
    if (rs == NULL) {
        return NULL;
    }
    if (rs->sleeps.elem_size == 0U &&
        vec_init(&rs->sleeps, sizeof(rs_sleep_t), 1U) != 0) {
        return NULL;
    }
    return &rs->sleeps;
}

/* 取当前 sleep 表项；未登记返回 NULL */
static rs_sleep_t *rs_sleep_find(roshell_t *rs)
{
    vec_t *v = rs_sleep_vec(rs);
    rs_sleep_t *w;

    if (v == NULL || vec_size(v) == 0U) {
        return NULL;
    }
    w = (rs_sleep_t *)vec_at(v, 0U);
    return (w != NULL && w->used) ? w : NULL;
}

/* 登记 sleep 表项（单表项，已有则复用）；无实例返回 NULL */
static rs_sleep_t *rs_sleep_alloc(roshell_t *rs)
{
    vec_t *v = rs_sleep_vec(rs);
    rs_sleep_t *w;

    if (v == NULL) {
        return NULL;
    }
    if (vec_size(v) == 0U && vec_resize(v, 1U) != 0) {
        return NULL;
    }
    w = (rs_sleep_t *)vec_at(v, 0U);
    if (w != NULL) {
        w->deadline = 0U;
        w->remain = 0U;
        w->paused = false;
    }
    return w;
}

/* 当前会话是否有 sleep 处于停放态（pause 命令据此跳过 OS 挂起） */
static bool rs_sleep_is_parked(roshell_t *rs)
{
    const rs_sleep_t *w = rs_sleep_find(rs);

    return (w != NULL) && w->paused;
}

static int rs_sleep_on_pause(roshell_t *rs, void *user)
{
    rs_sleep_t *w;
    uint32_t now;

    (void)user;
    w = rs_sleep_find(rs);
    if (w == NULL) {
        return -1;                      /* 未登记：调用方回退纯 OS 挂起 */
    }
    if (!w->paused) {
        now = ros_tick_get();
        w->remain = ROS_TICK_AFTER(w->deadline, now)
                        ? (w->deadline - now) : 0U;
        w->paused = true;
    }
    return 0;
}

static int rs_sleep_on_resume(roshell_t *rs, void *user)
{
    rs_sleep_t *w;

    (void)user;
    w = rs_sleep_find(rs);
    if (w == NULL) {
        return -1;
    }
    w->deadline = ros_tick_get() + w->remain;
    w->paused = false;
    return 0;
}

static int rs_sleep_on_stop(roshell_t *rs, void *user)
{
    rs_sleep_t *w;

    (void)user;
    w = rs_sleep_find(rs);
    if (w == NULL) {
        return 0;
    }
    w->paused = false;
    w->used = false;
    return 0;
}

/* sleep <ms>：持续命令（on_start 只登记截止时间，由执行任务轮转 on_tick）。
 * 暂停期间停放不消耗时间，恢复后只等剩余时间。 */
static int rs_cmd_sleep(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    uint32_t ms = 0U;
    rs_sleep_t *w;

    (void)parsed;
    (void)user;
    if (argc < 2 || !str_parse_uint(argv[1], &ms)) {
        rs_error(RS_ERR_PARAM, "usage: sleep <ms>");
        return -RS_ERR_PARAM;
    }
    w = rs_sleep_alloc(rs);
    if (w == NULL) {
        ros_sleep_ms(ms);               /* 无实例 / 表满：退化为不可暂停延时 */
        return 0;
    }
    w->deadline = ros_tick_get() + ms;
    w->used = true;                     /* used 最后置位，保证表项内容完整可见 */
    return RS_CMD_RUNNING;
}

/* sleep 的轮转回调：到期或被停止时返回 0（结束） */
static int rs_sleep_on_tick(roshell_t *rs, void *user)
{
    rs_sleep_t *w;

    (void)user;
    w = rs_sleep_find(rs);
    if (w == NULL) {
        return 0;                       /* on_stop/kill 已清表：结束 */
    }
    if (w->paused) {
        return RS_CMD_RUNNING;          /* 停放：不推进 deadline */
    }
    if (!ROS_TICK_AFTER(w->deadline, ros_tick_get())) {
        w->used = false;
        return 0;                       /* 正常到期 */
    }
    return RS_CMD_RUNNING;
}

/* wait [<cmd>] [timeout <ms>]：等待 '-' 异步启动的持续命令结束；
 * 不带 cmd 时等待全部异步命令。 */
int rs_cmd_wait(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    const char *name = NULL;
    uint32_t timeout = 0U;
    int i;
    int rc;

    (void)parsed;
    (void)user;
    if (argc >= 2 && strcmp(argv[1], "timeout") != 0) {
        name = argv[1];
    }
    for (i = 2; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "timeout") == 0) {
            uint32_t t;

            if (str_parse_uint(argv[i + 1], &t)) {
                timeout = t;
            }
        }
    }
    if (argc >= 3 && strcmp(argv[1], "timeout") == 0) {
        uint32_t t;

        if (str_parse_uint(argv[2], &t)) {
            timeout = t;
        }
    }

    rc = rs_cmd_wait_async(rs, rs_cmd_find_command("wait"), name, timeout);
    if (rc == -RS_ERR_TIMEOUT) {
        rs_error(RS_ERR_TIMEOUT, "wait timeout");
    }
    return rc;
}

/* 会话任务 / idle / 自身不允许 kill */
static bool rs_task_protected(roshell_t *rs, const ros_task_t *task)
{
    return (task == ros_current_task) || (task == ros_idle_task) ||
           (task == rs_session_task(rs));
}

int rs_cmd_kill(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    ros_task_t *task;

    (void)parsed;
    (void)user;
    if (argc < 2) {
        rs_error(RS_ERR_PARAM, "usage: kill <name|#tid>");
        return -RS_ERR_PARAM;
    }
    task = rs_find_task(argv[1]);
    if (task == NULL) {
        rs_error(RS_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -RS_ERR_NOT_FOUND;
    }
    if (rs_task_protected(rs, task)) {
        rs_error(RS_ERR_PARAM, "refusing to kill this task");
        return -RS_ERR_PARAM;
    }
    (void)rs_cmd_notify_kill(task);
    if (ros_task_destroy(task) != ROS_OK) {
        rs_error(RS_ERR_PARAM, "task busy or static");
        return -RS_ERR_PARAM;
    }
    ros_printf("task %s deleted\n", argv[1]);
    return 0;
}

int rs_cmd_pause(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    ros_task_t *task;
    int rc;

    (void)parsed;
    (void)user;
    if (argc < 2) {
        rs_error(RS_ERR_PARAM, "usage: pause <name|#tid>");
        return -RS_ERR_PARAM;
    }
    task = rs_find_task(argv[1]);
    if (task == NULL) {
        rs_error(RS_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -RS_ERR_NOT_FOUND;
    }
    if (task == ros_current_task || task == ros_idle_task ||
        task == rs_session_task(rs)) {
        rs_error(RS_ERR_PARAM, "refusing to pause this task");
        return -RS_ERR_PARAM;
    }
    /* 双路：先通知命令钩子。sleep 钩子命中（任务进入停放）则不做
     * OS 挂起；钩子返回 -1（未登记）回退 OS 挂起，其它负值拒绝暂停。 */
    rc = rs_cmd_notify_pause(task);
    if (rs_sleep_is_parked(rs)) {
        return 0;
    }
    if (rc < 0 && rc != -1) {
        rs_error(RS_ERR_PARAM, "command refused pause");
        return -RS_ERR_PARAM;
    }
    if (ros_task_suspend(task) != ROS_OK) {
        (void)rs_cmd_notify_resume(task);
        rs_error(RS_ERR_PARAM, "task cannot be paused");
        return -RS_ERR_PARAM;
    }
    return 0;
}

int rs_cmd_resume(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    ros_task_t *task;

    (void)parsed;
    (void)user;
    if (argc < 2) {
        rs_error(RS_ERR_PARAM, "usage: resume <name|#tid>");
        return -RS_ERR_PARAM;
    }
    task = rs_find_task(argv[1]);
    if (task == NULL) {
        rs_error(RS_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -RS_ERR_NOT_FOUND;
    }
    /* 双路：sleep 停放态没有 OS 挂起，直接通知钩子解除停放 */
    if (rs_sleep_is_parked(rs)) {
        (void)rs_cmd_notify_resume(task);
        return 0;
    }
    if (ros_task_resume(task) != ROS_OK) {
        rs_error(RS_ERR_PARAM, "task is not suspended");
        return -RS_ERR_PARAM;
    }
    (void)rs_cmd_notify_resume(task);
    return 0;
}

static void rs_mem_line(void)
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

int rs_cmd_mem(roshell_t *rs, int argc, char **argv,
               const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)parsed;
    (void)user;
    rs_mem_line();
    return 0;
}

int rs_cmd_state(roshell_t *rs, int argc, char **argv,
                 const rs_args_parsed_t *parsed, void *user)
{
    uint32_t ready = 0U;
    uint32_t running = 0U;
    uint32_t blocked = 0U;
    uint32_t suspended = 0U;
    uint32_t total = 0U;
    ros_stats_t sys;
    ros_task_t *task;
    uint32_t idle_pm;
    uint32_t busy_pm;

    (void)rs;
    (void)argc;
    (void)argv;
    (void)parsed;
    (void)user;

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

    rs_mem_line();
    return 0;
}

int rs_cmd_exit(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)argc;
    (void)argv;
    (void)parsed;
    (void)user;
    ros_puts("bye");
    rs_shell_request_exit(rs);
    return 0;
}

/* ------------------------------ help ------------------------------ */

static const rs_cmd_t s_cmds[] = {
    { "help",    NULL, "help [cmd]",                 "basic",   NULL, 0U,
      rs_builtin_help, NULL, NULL, NULL, NULL, NULL },
    { "ver",     NULL, "ver",                        "basic",   NULL, 0U,
      rs_cmd_ver,      NULL, NULL, NULL, NULL, NULL },
    { "echo",    NULL, "echo <text|$var>...",        "basic",   NULL, 0U,
      rs_cmd_echo,     NULL, NULL, NULL, NULL, NULL },
    { "ps",      NULL, "ps",                         "task",    NULL, 0U,
      rs_cmd_ps,       NULL, NULL, NULL, NULL, NULL },
    { "kill",    NULL, "kill <name|#tid>",           "task",    NULL, 0U,
      rs_cmd_kill,     NULL, NULL, NULL, NULL, NULL },
    { "pause",   NULL, "pause <name|#tid>",          "task",    NULL, 0U,
      rs_cmd_pause,    NULL, NULL, NULL, NULL, NULL },
    { "resume",  NULL, "resume <name|#tid>",         "task",    NULL, 0U,
      rs_cmd_resume,   NULL, NULL, NULL, NULL, NULL },
    { "mem",     NULL, "mem",                        "sys",     NULL, 0U,
      rs_cmd_mem,      NULL, NULL, NULL, NULL, NULL },
    { "state",   NULL, "state",                      "sys",     NULL, 0U,
      rs_cmd_state,    NULL, NULL, NULL, NULL, NULL },
    { "sleep",   NULL, "sleep <ms>",                 "run",     NULL, 0U,
      rs_cmd_sleep,    rs_sleep_on_tick, rs_sleep_on_pause, rs_sleep_on_resume,
      rs_sleep_on_stop, NULL },
    { "wait",    NULL, "wait [cmd] [timeout <ms>]",  "run",     NULL, 0U,
      rs_cmd_wait,     NULL, NULL, NULL, NULL, NULL },
    { "exit",    NULL, "exit",                       "session", NULL, 0U,
      rs_cmd_exit,     NULL, NULL, NULL, NULL, NULL }
};

#define RS_CMD_COUNT (sizeof(s_cmds) / sizeof(s_cmds[0]))

/* 内建命令静态表（命令表初始化时登记到全局 map）与归属判断 */
const rs_cmd_t *rs_builtin_table(uint32_t *count)
{
    if (count != NULL) {
        *count = (uint32_t)RS_CMD_COUNT;
    }
    return s_cmds;
}

bool rs_builtin_is(const rs_cmd_t *def)
{
    uint32_t i;

    if (def == NULL) {
        return false;
    }
    for (i = 0U; i < RS_CMD_COUNT; i++) {
        if (def == &s_cmds[i]) {
            return true;
        }
    }
    return false;
}

/* 内建命令查找（全局命令表中命中且属于内建表），大小写敏感 */
const rs_cmd_t *rs_builtin_find(const char *name)
{
    const rs_cmd_t *def = rs_cmd_find_command(name);

    return rs_builtin_is(def) ? def : NULL;
}

int rs_builtin_help(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    uint32_t i;

    (void)rs;
    (void)parsed;
    (void)user;

    if (argc >= 2) {
        const rs_cmd_t *cmd = rs_cmd_find_command(argv[1]);

        if (cmd != NULL) {
            char buf[160];

            /* 内建命令只回显 usage；扩展命令输出完整帮助 */
            if (rs_builtin_is(cmd)) {
                if (rs_cmd_usage(cmd, buf, sizeof(buf)) > 0) {
                    ros_printf("%s\n", buf);
                }
                return 0;
            }
            if (rs_cmd_help(cmd, buf, sizeof(buf)) > 0) {
                (void)ros_io_write(NULL, buf, (uint32_t)strlen(buf));
                return 0;
            }
        }
        rs_error(RS_ERR_NOT_FOUND, "no help for '%s'", argv[1]);
        return -RS_ERR_NOT_FOUND;
    }

    ros_printf("Commands:\n");
    for (i = 0U; i < RS_CMD_COUNT; i++) {
        char buf[128];

        if (rs_cmd_usage(&s_cmds[i], buf, sizeof(buf)) > 0) {
            ros_printf("  %-28s %s\n", buf, s_cmds[i].group);
        }
    }
    {
        const rs_cmd_t *def;

        for (def = rs_cmd_first(); def != NULL; def = rs_cmd_next(def)) {
            char buf[128];

            if (rs_builtin_is(def)) {
                continue;               /* 内建命令已按分组输出 */
            }
            if (rs_cmd_usage(def, buf, sizeof(buf)) > 0) {
                ros_printf("  %-28s %s\n", buf,
                           def->desc != NULL ? def->desc : "");
            }
        }
    }
    ros_puts("script lines (non-command) run on the roshell engine");
    return 0;
}
