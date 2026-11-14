/*
 * shell_cmd.c - Shell 命令（SHELL_CTX）
 *
 * 覆盖 V1.1 §2~§4：基础命令、RTOS 任务管理、JOB 生命周期。
 * 设备命令（GPIO/PWM/步进电机）由 shell_dev.c 提供，打开 Shell 后自动注册。
 */

#include "shell_internal.h"
#include "cmd.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define SHELL_TAG "shell"

/* ============================== 输出辅助 ============================== */

void shell_error(shell_t *sh, int code, const char *fmt, ...)
{
    char msg[64];
    va_list ap;
    int n;

    (void)sh;
    va_start(ap, fmt);
    n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) {
        msg[0] = '\0';
    }
    ros_printf("ERR %d: %s\n", code, msg);
}

static void shell_warn(shell_t *sh, const char *fmt, ...)
{
    char msg[64];
    va_list ap;

    (void)sh;
    va_start(ap, fmt);
    (void)vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    ros_printf("WARN: %s\n", msg);
}

/* 把 argv[from..] 用空格拼成一行（用于表达式 / echo 文本） */
static uint16_t shell_join(char *buf, uint16_t cap, int argc, char **argv,
                           int from)
{
    uint16_t n = 0U;
    int i;

    buf[0] = '\0';
    for (i = from; i < argc; i++) {
        uint16_t len = (uint16_t)strlen(argv[i]);

        if (n + len + 2U >= cap) {
            break;
        }
        if (i > from) {
            buf[n++] = ' ';
        }
        memcpy(&buf[n], argv[i], len);
        n = (uint16_t)(n + len);
        buf[n] = '\0';
    }
    return n;
}

/* ============================== 任务 / JOB 查找 ============================== */

static bool shell_parse_tid(const char *s, uint16_t *tid)
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

static ros_task_t *shell_find_task(const char *arg)
{
    ros_task_t *task;
    uint16_t tid = 0U;

    if (arg == NULL) {
        return NULL;
    }
    if (shell_parse_tid(arg, &tid)) {
        for (task = ros_task_first(); task != NULL; task = ros_task_next(task)) {
            if (task->tid == tid) {
                return task;
            }
        }
        return NULL;
    }
    for (task = ros_task_first(); task != NULL; task = ros_task_next(task)) {
        if (task->name != NULL &&
            shell_ieq(task->name, (uint16_t)strlen(task->name), arg) &&
            strlen(task->name) == strlen(arg)) {
            return task;
        }
    }
    return NULL;
}

static const char *shell_task_state_name(ros_task_state_e state)
{
    switch (state) {
    case ROS_TASK_READY:      return "READY";
    case ROS_TASK_RUNNING:    return "RUNNING";
    case ROS_TASK_BLOCKED:    return "BLOCKED";
    case ROS_TASK_DELAYED:    return "BLOCKED";
    case ROS_TASK_SUSPENDED:  return "SUSPENDED";
    case ROS_TASK_TERMINATED: return "DELETED";
    default:                  return "?";
    }
}

/* ============================== 基础命令 ============================== */

static int builtin_help(shell_t *sh, int argc, char **argv);

static int cmd_ver(shell_t *sh, int argc, char **argv)
{
    char info[512];

    (void)argc;
    (void)argv;
    ros_printf("IoT Shell v1.1\n");
    ros_printf("Build: %s\n",
                 sh->cfg.version != NULL ? sh->cfg.version : "-");

    /* 版本 / 编译时间 / 平台 / 编译器 / 内核参数 / 许可证 / 特性 */
    {
        int n = ros_version_format(info, sizeof(info));

        if (n > 0) {
            /* 多行摘要可能超过 ros_printf 的单次格式化缓冲，直接写 io */
            ros_io_write(NULL, info, (uint32_t)n);
        }
    }

    ros_printf("FS: %s | CPU stats: %s\n",
                 SHELL_HAS_FS ? "enabled" : "disabled",
                 SHELL_HAS_CPU_STATS ? "enabled" : "disabled");
    return SHELL_OK;
}

/* echo：逐个 token 做 $VAR 替换，引号内的内容原样输出，不求值 */
static int cmd_echo(shell_t *sh, int argc, char **argv)
{
    int a;
    bool first = true;

    for (a = 1; a < argc; a++) {
        const char *tok = argv[a];
        uint16_t len = (uint16_t)strlen(tok);
        uint16_t i = 0U;

        /* 去掉包裹的引号 */
        if (len >= 2U && tok[0] == '"' && tok[len - 1U] == '"') {
            tok++;
            len = (uint16_t)(len - 2U);
        }
        if (!first) {
            ros_putchar(' ');
        }
        first = false;

        while (i < len) {
            if (tok[i] == '$' && i + 1U < len) {
                uint16_t start = (uint16_t)(i + 1U);
                uint16_t end = start;
                shell_value_t val;
                shell_err_t err;
                char buf[96];
                uint16_t out_len = 0U;

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
                if (shell_vars_get(&sh->vars, &tok[start],
                                   (uint16_t)(end - start), &val, &err) == 0) {
                    shell_value_format(&val, buf, sizeof(buf), &out_len);
                    ros_io_write(NULL, buf, out_len);
                } else {
                    shell_warn(sh, "undefined variable $%.*s", (int)(end - start),
                               &tok[start]);
                    ros_putchar('$');
                    ros_io_write(NULL, &tok[start], (uint32_t)(end - start));
                }
                i = end;
                continue;
            }
            ros_putchar(tok[i]);
            i++;
        }
    }
    ros_putchar('\n');
    return SHELL_OK;
}

static int cmd_print(shell_t *sh, int argc, char **argv)
{
    char text[SHELL_LINE_MAX];
    shell_tok_t toks[SHELL_TOKEN_MAX];
    shell_value_t val;
    shell_err_t err;
    char buf[96];
    uint16_t out_len = 0U;
    uint16_t len;
    int n;

    len = shell_join(text, sizeof(text), argc, argv, 1);
    n = shell_lex(text, len, toks, SHELL_TOKEN_MAX);
    memset(&err, 0, sizeof(err));
    if (shell_expr_eval(toks, (uint16_t)n, &sh->vars, &val, &err) != 0) {
        shell_report_error(sh, &err, NULL);
        return -err.code;
    }
    shell_value_format(&val, buf, sizeof(buf), &out_len);
    ros_printf("%.*s\n", (int)out_len, buf);
    return SHELL_OK;
}

static int cmd_set(shell_t *sh, int argc, char **argv)
{
    char text[SHELL_LINE_MAX];
    shell_tok_t toks[SHELL_TOKEN_MAX];
    shell_value_t val;
    shell_err_t err;
    uint16_t len;
    int n;
    int eq = -1;
    int i;

    len = shell_join(text, sizeof(text), argc, argv, 1);
    n = shell_lex(text, len, toks, SHELL_TOKEN_MAX);
    if (n < 4 || toks[0].type != SHELL_TOK_VAR) {
        shell_error(sh, SHELL_ERR_PARSE, "SET expects $VAR = expr");
        return -SHELL_ERR_PARSE;
    }
    for (i = 1; i < n - 1; i++) {
        if (toks[i].type == SHELL_TOK_OP && toks[i].len == 1U &&
            toks[i].p[0] == '=') {
            eq = i;
            break;
        }
    }
    if (eq < 0) {
        shell_error(sh, SHELL_ERR_PARSE, "SET expects $VAR = expr");
        return -SHELL_ERR_PARSE;
    }
    memset(&err, 0, sizeof(err));
    if (shell_expr_eval(&toks[eq + 1], (uint16_t)(n - eq - 1),
                        &sh->vars, &val, &err) != 0) {
        shell_report_error(sh, &err, NULL);
        return -err.code;
    }
    if (shell_vars_set(&sh->vars, toks[0].p + 1,
                       (uint16_t)(toks[0].len - 1U), &val, false,
                       &err) != 0) {
        shell_report_error(sh, &err, NULL);
        return -err.code;
    }
    return SHELL_OK;
}

static int cmd_unset(shell_t *sh, int argc, char **argv)
{
    const char *name;

    if (argc < 2 || argv[1][0] != '$') {
        shell_error(sh, SHELL_ERR_PARAM, "usage: unset $VAR");
        return -SHELL_ERR_PARAM;
    }
    name = &argv[1][1];
    if (shell_vars_unset(&sh->vars, name, (uint16_t)strlen(name)) != 0) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "variable not found");
        return -SHELL_ERR_NOT_FOUND;
    }
    return SHELL_OK;
}

/* 可被 Ctrl-C 打断的毫秒等待 */
static bool shell_wait_ms(shell_t *sh, uint32_t ms)
{
    uint32_t elapsed = 0U;

    while (elapsed < ms) {
        uint32_t step = (ms - elapsed > 20U) ? 20U : (ms - elapsed);

        if (shell_ctrl_c_pending(sh)) {
            shell_clear_ctrl_c(sh);
            ros_puts("WARN: wait interrupted");
            return false;
        }
        ros_sleep_ms(step);
        elapsed += step;
    }
    return true;
}

static int cmd_wait(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: wait <time> | wait task <name>");
        return -SHELL_ERR_PARAM;
    }
    if (strcmp(argv[1], "task") == 0) {
        ros_task_t *task;
        uint32_t timeout = SHELL_WAIT_DEFAULT_MS;
        uint32_t waited = 0U;
        int i;

        if (argc < 3) {
            shell_error(sh, SHELL_ERR_PARAM, "usage: wait task <name> [timeout <t>]");
            return -SHELL_ERR_PARAM;
        }
        for (i = 3; i + 1 < argc; i += 2) {
            if (strcmp(argv[i], "timeout") == 0) {
                timeout = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            }
        }
        task = shell_find_task(argv[2]);
        if (task == NULL) {
            shell_error(sh, SHELL_ERR_NOT_FOUND, "task %s not found", argv[2]);
            return -SHELL_ERR_NOT_FOUND;
        }
        if ((task->name != NULL && strcmp(task->name, "shell") == 0) ||
            task == ros_idle_task) {
            shell_error(sh, SHELL_ERR_PARAM, "cannot wait on shell/idle");
            return -SHELL_ERR_PARAM;
        }
        while (task->state != ROS_TASK_TERMINATED) {
            if (timeout != 0U && waited >= timeout) {
                shell_error(sh, SHELL_ERR_TIMEOUT, "wait task %s timeout", argv[2]);
                return -SHELL_ERR_TIMEOUT;
            }
            if (shell_ctrl_c_pending(sh)) {
                shell_clear_ctrl_c(sh);
                ros_puts("WARN: wait interrupted");
                return SHELL_OK;
            }
            ros_sleep_ms(10U);
            waited += 10U;
        }
        return SHELL_OK;
    }

    /* wait <time>：表达式，单位固定为 ms */
    {
        char text[32];
        shell_tok_t toks[8];
        shell_value_t val;
        shell_err_t err;
        int32_t ms = 0;
        int n;
        uint16_t len;

        (void)snprintf(text, sizeof(text), "%s", argv[1]);
        len = (uint16_t)strlen(text);
        n = shell_lex(text, len, toks, 8);
        memset(&err, 0, sizeof(err));
        if (shell_expr_eval(toks, (uint16_t)n, &sh->vars, &val, &err) != 0 ||
            shell_value_to_int(&val, &ms, &err) != 0) {
            shell_error(sh, SHELL_ERR_PARAM, "invalid argument for wait");
            return -SHELL_ERR_PARAM;
        }
        (void)shell_wait_ms(sh, (ms > 0) ? (uint32_t)ms : 0U);
    }
    return SHELL_OK;
}

static int cmd_state(shell_t *sh, int argc, char **argv)
{
    uint32_t ready = 0U;
    uint32_t running = 0U;
    uint32_t blocked = 0U;
    uint32_t suspended = 0U;
    uint32_t total = 0U;
    uint32_t stack_total = 0U;
    uint32_t stack_used = 0U;
#if ROS_USE_MEM_STATS
    uint32_t mem_task_used = 0U;
    uint32_t mem_task_peak = 0U;
#endif
    ros_task_t *task;
    uint16_t i;

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

        stack_total += task->stack_size;
#if ROS_USE_MEM_STATS
        mem_task_used += task->mem_used;
        mem_task_peak += task->mem_peak;
#endif
#if ROS_USE_STACK_CHECK
        {
            ros_task_stats_t st;

            if (ros_task_get_stats(task, &st) == ROS_OK &&
                st.stack_free_bytes <= task->stack_size) {
                stack_used += task->stack_size - st.stack_free_bytes;
            }
        }
#endif
    }
    ros_printf("RTOS Tasks: %u (RUNNING=%u READY=%u BLOCKED=%u SUSPENDED=%u)\n",
                 (unsigned)total, (unsigned)running, (unsigned)ready,
                 (unsigned)blocked, (unsigned)suspended);

    /* CPU / 栈 / 堆概览 */
    {
        ros_stats_t sys;
        ros_mem_info_t mem = { 0 };
        uint32_t uptime_s;

        (void)ros_stats_get(&sys);
        uptime_s = sys.tick_count / 1000U;
#if SHELL_HAS_CPU_STATS
        {
            uint32_t idle_pm = (ros_idle_task != NULL)
                ? ros_task_cpu_permille(ros_idle_task) : 0U;
            uint32_t busy_pm = (idle_pm <= 1000U) ? (1000U - idle_pm) : 0U;

            ros_printf("CPU       : %u.%u%% busy, uptime=%us, ctx=%u\n",
                         (unsigned)(busy_pm / 10U), (unsigned)(busy_pm % 10U),
                         (unsigned)uptime_s, (unsigned)sys.ctx_switches);
        }
#else
        ros_printf("CPU       : n/a, uptime=%us, ctx=%u\n",
                     (unsigned)uptime_s, (unsigned)sys.ctx_switches);
#endif
#if ROS_USE_STACK_CHECK
        if (stack_total > 0U) {
            uint32_t pm = (uint32_t)(((uint64_t)stack_used * 1000U) /
                                     stack_total);

            ros_printf("STACK     : %u/%u B (%u.%u%%)\n",
                         (unsigned)stack_used, (unsigned)stack_total,
                         (unsigned)(pm / 10U), (unsigned)(pm % 10U));
        }
#endif
        if (ros_mem_get_info(&mem) != 0) {
            ros_printf("MEM-heap  : n/a\n");
        } else if (mem.total_bytes > 0U) {
            uint32_t pm = (uint32_t)(((uint64_t)mem.used_bytes * 1000U) /
                                     mem.total_bytes);

            ros_printf("MEM-heap  : %u/%u B (%u.%u%%), free %u B\n",
                         (unsigned)mem.used_bytes,
                         (unsigned)mem.total_bytes,
                         (unsigned)(pm / 10U), (unsigned)(pm % 10U),
                         (unsigned)mem.free_bytes);
        } else if (mem.used_bytes > 0U) {
            ros_printf("MEM-heap  : %u B used (capacity n/a)\n",
                         (unsigned)mem.used_bytes);
        } else {
            ros_printf("MEM-heap  : n/a\n");
        }
#if ROS_USE_MEM_STATS
        ros_printf("MEM-tasks : %u B (peak %u B)\n",
                     (unsigned)mem_task_used, (unsigned)mem_task_peak);
        if (mem.untracked_bytes > 0U) {
            ros_printf("MEM-sys   : %u B (no task context)\n",
                         (unsigned)mem.untracked_bytes);
        }
#endif
    }
    ros_printf("JOBs      :");
    {
        bool any = false;
        shell_job_t *job;

        for (job = shell_job_first(sh); job != NULL;
             job = shell_job_next(job)) {
            any = true;
            ros_printf(" %s=%s", job->name,
                         shell_job_state_name(job->state));
        }
        if (!any) {
            ros_printf(" (none)");
        }
    }
    ros_printf("\n");
    ros_printf("Shell Vars:");
    for (i = 0U; i < vec_size(&sh->vars.items); i++) {
        const shell_var_t *v = (const shell_var_t *)vec_at(&sh->vars.items, i);

        if (v->used) {
            ros_printf(" %s=", v->name);
            if (v->type == SHELL_VAL_STRING) {
                ros_printf("%s", v->str);
            } else if (v->type == SHELL_VAL_INT) {
                ros_printf("%ld", (long)v->i);
            } else if (v->type == SHELL_VAL_FLOAT) {
                ros_printf("%g", (double)v->f);
            } else {
                ros_printf("%s", v->b ? "TRUE" : "FALSE");
            }
        }
    }
    ros_printf("\n");
    return SHELL_OK;
}

static int cmd_reboot(shell_t *sh, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (sh->cfg.reset == NULL) {
        shell_error(sh, SHELL_ERR_PARAM, "reset not supported");
        return -SHELL_ERR_PARAM;
    }
    ros_puts("rebooting...");
    sh->cfg.reset();
    return SHELL_OK;
}

/* ------------------------------ ps ------------------------------ */

static void shell_ps_line(shell_t *sh, const ros_task_t *task)
{
    shell_job_t *job = shell_job_find_by_task(sh, task);
    const char *type = (job != NULL) ? "JOB" : "SYS";
    const char *state = shell_task_state_name(task->state);
    char cpu[8] = "-";
    char stack[8];
    char used[8] = "-";
    char use[8] = "-";
    char mem[10] = "-";

    (void)snprintf(stack, sizeof(stack), "%u", (unsigned)task->stack_size);
#if SHELL_HAS_CPU_STATS
    {
        uint32_t pm = ros_task_cpu_permille(task);

        (void)snprintf(cpu, sizeof(cpu), "%u.%u", (unsigned)(pm / 10U),
                       (unsigned)(pm % 10U));
    }
#endif
#if ROS_USE_STACK_CHECK
    if (task->stack_size > 0U) {
        ros_task_stats_t st;

        if (ros_task_get_stats(task, &st) == ROS_OK &&
            st.stack_free_bytes <= task->stack_size) {
            uint32_t bytes = task->stack_size - st.stack_free_bytes;
            uint32_t pm = (uint32_t)(((uint64_t)bytes * 1000U) /
                                     task->stack_size);

            (void)snprintf(used, sizeof(used), "%u", (unsigned)bytes);
            (void)snprintf(use, sizeof(use), "%u.%u%%", (unsigned)(pm / 10U),
                           (unsigned)(pm % 10U));
        }
    }
#endif
#if ROS_USE_MEM_STATS
    (void)snprintf(mem, sizeof(mem), "%u", (unsigned)task->mem_used);
#endif
    ros_printf("%3u  %-8s  %-4s  %-9s  %4u  %5s  %4s  %5s  %6s  %5s  %s\n",
                 (unsigned)task->tid,
                 task->name != NULL ? task->name : "?",
                 type, state, (unsigned)task->priority, stack, used, use, mem,
                 cpu, job != NULL ? job->name : "-");
}

static int cmd_ps(shell_t *sh, int argc, char **argv)
{
    ros_task_t *task;
    bool show_all = false;
    bool only_job = false;
    bool only_sys = false;

    if (argc >= 2 && strcmp(argv[1], "-a") == 0) {
        show_all = true;
    } else if (argc >= 2 && strcmp(argv[1], "job") == 0) {
        only_job = true;
    } else if (argc >= 2 && strcmp(argv[1], "sys") == 0) {
        only_sys = true;
    }

    ros_printf("TID  NAME      TYPE   STATE      PRIO  STACK  USED  USE%%   MEM     CPU%%   JOB\n");
    ros_printf("---  --------  ----  ---------  ----  -----  ----  -----  ------  -----  ----\n");
    for (task = ros_task_first(); task != NULL; task = ros_task_next(task)) {
        shell_job_t *job = shell_job_find_by_task(sh, task);

        if (!show_all && task->state == ROS_TASK_TERMINATED) {
            continue;
        }
        if (only_job && job == NULL) {
            continue;
        }
        if (only_sys && job != NULL) {
            continue;
        }
        shell_ps_line(sh, task);
    }
    return SHELL_OK;
}

/* ------------------------------ kill / pause / resume ------------------------------ */

static bool shell_task_is_protected(const ros_task_t *task)
{
    return (task == ros_current_task) || (task == ros_idle_task) ||
           (task->name != NULL && strcmp(task->name, "shell") == 0);
}

static int cmd_kill(shell_t *sh, int argc, char **argv)
{
    ros_task_t *task;
    shell_job_t *job;

    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: kill <task|#tid>");
        return -SHELL_ERR_PARAM;
    }
    task = shell_find_task(argv[1]);
    if (task == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (shell_task_is_protected(task)) {
        shell_error(sh, SHELL_ERR_DANGER, "refusing to kill shell/idle");
        return -SHELL_ERR_DANGER;
    }
    job = shell_job_find_by_task(sh, task);
    if (job != NULL) {
        /* JOB 停止流程内部会通知 on_kill */
        (void)shell_job_stop(job, true);
        job->state = JOB_ABORTED;
        return SHELL_OK;
    }
    /* 系统任务：先通知命令的 on_kill，再删除 */
    (void)cmd_notify_kill(task);
    if (ros_task_destroy(task) != ROS_OK) {
        shell_error(sh, SHELL_ERR_STATE, "task busy or not destroyable");
        return -SHELL_ERR_STATE;
    }
    shell_warn(sh, "system task deleted");
    return SHELL_OK;
}

static int cmd_pause(shell_t *sh, int argc, char **argv)
{
    ros_task_t *task;
    shell_job_t *job;
    int rc;

    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: pause <task|#tid>");
        return -SHELL_ERR_PARAM;
    }
    task = shell_find_task(argv[1]);
    if (task == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (shell_task_is_protected(task)) {
        shell_error(sh, SHELL_ERR_DANGER, "refusing to pause shell/idle");
        return -SHELL_ERR_DANGER;
    }
    /* 命令可选择拒绝暂停（例如无法保持当前状态） */
    rc = cmd_notify_pause(task);
    if (rc < 0) {
        shell_error(sh, SHELL_ERR_STATE, "command refused pause");
        return -SHELL_ERR_STATE;
    }
    if (ros_task_suspend(task) != ROS_OK) {
        (void)cmd_notify_resume(task);      /* 回滚 pause 钩子 */
        shell_error(sh, SHELL_ERR_STATE, "task cannot be paused");
        return -SHELL_ERR_STATE;
    }
    job = shell_job_find_by_task(sh, task);
    if (job != NULL) {
        job->state = JOB_PAUSED;
        /* 父 JOB 暂停：同步挂起它启动的子 JOB */
        shell_job_pause_descendants(job);
    }
    return SHELL_OK;
}

static int cmd_resume(shell_t *sh, int argc, char **argv)
{
    ros_task_t *task;
    shell_job_t *job;

    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: resume <task|#tid>");
        return -SHELL_ERR_PARAM;
    }
    task = shell_find_task(argv[1]);
    if (task == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (ros_task_resume(task) != ROS_OK) {
        shell_error(sh, SHELL_ERR_STATE, "task is not suspended");
        return -SHELL_ERR_STATE;
    }
    (void)cmd_notify_resume(task);
    job = shell_job_find_by_task(sh, task);
    if (job != NULL) {
        job->state = JOB_RUNNING;
        /* 父 JOB 恢复：同步恢复它启动的子 JOB */
        shell_job_resume_descendants(job);
    }
    return SHELL_OK;
}

/* ------------------------------ JOB 管理 ------------------------------ */

static int cmd_load(shell_t *sh, int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if SHELL_HAS_FS
    shell_error(sh, SHELL_ERR_NOT_FOUND, "load not implemented");
#else
    shell_error(sh, SHELL_ERR_NOT_FOUND, "file system not available");
#endif
    return -SHELL_ERR_NOT_FOUND;
}

static int cmd_unload(shell_t *sh, int argc, char **argv)
{
    shell_job_t *job;

    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: unload <name>");
        return -SHELL_ERR_PARAM;
    }
    job = shell_job_find(sh, argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (job->state == JOB_RUNNING || job->state == JOB_PAUSED ||
        job->state == JOB_STOPPING) {
        shell_error(sh, SHELL_ERR_STATE, "job %s is %s", argv[1],
                    shell_job_state_name(job->state));
        return -SHELL_ERR_STATE;
    }
    shell_job_unload(job);
    return SHELL_OK;
}

static int cmd_loads(shell_t *sh, int argc, char **argv)
{
    shell_job_t *job;
    bool any = false;

    (void)argc;
    (void)argv;
    ros_printf("NAME   STATE      SCRIPT\n");
    for (job = shell_job_first(sh); job != NULL; job = shell_job_next(job)) {
        any = true;
        ros_printf("%-6s %-10s %s\n", job->name,
                     shell_job_state_name(job->state),
                     job->text_len > 0U ? "(inline)" : "-");
    }
    if (!any) {
        ros_puts("(no jobs)");
    }
    return SHELL_OK;
}

static int cmd_jobs(shell_t *sh, int argc, char **argv)
{
    shell_job_t *job;
    bool any = false;

    (void)argc;
    (void)argv;
    ros_printf("NAME   TASK   TID  STATE      PC\n");
    for (job = shell_job_first(sh); job != NULL; job = shell_job_next(job)) {
        if (job->state == JOB_RUNNING || job->state == JOB_PAUSED ||
            job->state == JOB_STOPPING) {
            any = true;
            ros_printf("%-6s %-6s %3u  %-10s L%u\n", job->name, job->name,
                         (unsigned)(job->task != NULL ? job->task->tid : 0U),
                         shell_job_state_name(job->state), (unsigned)job->pc);
        }
    }
    if (!any) {
        ros_puts("(no jobs)");
    }
    return SHELL_OK;
}

static int cmd_job(shell_t *sh, int argc, char **argv)
{
    shell_job_t *job;
    uint16_t i;

    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: job <name>");
        return -SHELL_ERR_PARAM;
    }
    job = shell_job_find(sh, argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    ros_printf("Name    : %s\n", job->name);
    ros_printf("JobID   : %u\n", (unsigned)job->job_id);
    ros_printf("TID     : %u\n",
                 (unsigned)(job->task != NULL ? job->task->tid : 0U));
    ros_printf("State   : %s\n", shell_job_state_name(job->state));
    ros_printf("Prio    : %u\n", (unsigned)job->prio);
    ros_printf("Stack   : %u\n", (unsigned)job->stack);
    ros_printf("PC      : L%u\n", (unsigned)job->pc);
    ros_printf("Vars    :");
    for (i = 0U; i < vec_size(&job->vars.items); i++) {
        const shell_var_t *v = (const shell_var_t *)vec_at(&job->vars.items, i);
        if (v->used) {
            ros_printf(" $%s", v->name);
        }
    }
    ros_printf("\n");
    return SHELL_OK;
}

static int shell_job_cmd_start(shell_t *sh, const char *name)
{
    shell_job_t *job = shell_job_find(sh, name, (uint16_t)strlen(name));

    if (job == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "job %s not found", name);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (job->state == JOB_RUNNING || job->state == JOB_PAUSED) {
        shell_error(sh, SHELL_ERR_STATE, "job %s is %s", name,
                    shell_job_state_name(job->state));
        return -SHELL_ERR_STATE;
    }
    if (shell_job_start(job) != 0) {
        shell_error(sh, SHELL_ERR_STATE, "job %s is not LOADED", name);
        return -SHELL_ERR_STATE;
    }
    return SHELL_OK;
}

static int cmd_start(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: start <name>");
        return -SHELL_ERR_PARAM;
    }
    return shell_job_cmd_start(sh, argv[1]);
}

static int cmd_stop(shell_t *sh, int argc, char **argv)
{
    shell_job_t *job;
    bool force = false;
    int i;

    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: stop <name> [force]");
        return -SHELL_ERR_PARAM;
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "force") == 0) {
            force = true;
        }
    }
    job = shell_job_find(sh, argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (shell_job_stop(job, force) != 0) {
        shell_error(sh, SHELL_ERR_STATE, "job %s is not running", argv[1]);
        return -SHELL_ERR_STATE;
    }
    return SHELL_OK;
}

static int cmd_fg(shell_t *sh, int argc, char **argv)
{
    shell_job_t *job;
    uint32_t timeout = 0U;

    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: fg <name>");
        return -SHELL_ERR_PARAM;
    }
    job = shell_job_find(sh, argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    job->foreground = true;
    if (job->state == JOB_LOADED) {
        if (shell_job_start(job) != 0) {
            shell_error(sh, SHELL_ERR_STATE, "job %s start failed", argv[1]);
            return -SHELL_ERR_STATE;
        }
    }
    if (shell_job_wait(job, timeout) != 0) {
        shell_error(sh, SHELL_ERR_TIMEOUT, "fg %s timeout", argv[1]);
        return -SHELL_ERR_TIMEOUT;
    }
    return SHELL_OK;
}

static int cmd_waitjob(shell_t *sh, int argc, char **argv)
{
    shell_job_t *job;
    uint32_t timeout = SHELL_WAIT_DEFAULT_MS;
    int i;

    if (argc < 2) {
        shell_error(sh, SHELL_ERR_PARAM, "usage: waitjob <name> [timeout <t>]");
        return -SHELL_ERR_PARAM;
    }
    for (i = 2; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "timeout") == 0) {
            timeout = (uint32_t)strtoul(argv[i + 1], NULL, 10);
        }
    }
    job = shell_job_find(sh, argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(sh, SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (job->state != JOB_RUNNING && job->state != JOB_PAUSED &&
        job->state != JOB_STOPPING) {
        return SHELL_OK;        /* 已结束：立即返回 */
    }
    if (shell_job_wait(job, timeout) != 0) {
        shell_error(sh, SHELL_ERR_TIMEOUT, "waitjob %s timeout", argv[1]);
        return -SHELL_ERR_TIMEOUT;
    }
    return SHELL_OK;
}

/* run：在独立的虚拟 Shell 实例中解释执行（默认执行粘贴缓冲） */
static int builtin_run(shell_t *sh, int argc, char **argv)
{
    bool background = false;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "&") == 0) {
            background = true;
        }
    }
    (void)background;       /* 同步执行；保留语法兼容 */

    if (argc < 2 || argv[1][0] == '&') {
        uint32_t len = 0U;
        const char *text = shell_paste_buffer(sh, &len);
        int rc;

        if (len == 0U) {
            ros_puts("(empty)");
            return SHELL_OK;
        }
        rc = shell_exec_isolated(sh, text);
        sh->paste_len = 0U;         /* 已交给子实例 */
        sh->block_depth = 0;
        return rc;
    }
#if !SHELL_HAS_FS
    shell_error(sh, SHELL_ERR_NOT_FOUND, "file system not available");
    return -SHELL_ERR_NOT_FOUND;
#else
    shell_error(sh, SHELL_ERR_NOT_FOUND, "run <file> not implemented");
    return -SHELL_ERR_NOT_FOUND;
#endif
}

/* ------------------------------ help ------------------------------ */

static const shell_cmd_t s_builtin[] = {
    { "help",   builtin_help,   "help [topic]", "", "basic", CMD_NONE },
    { "ver",    cmd_ver,    "ver", "", "basic", CMD_REENTRANT },
    { "echo",   cmd_echo,   "echo <text>", "", "basic", CMD_NONE },
    { "print",  cmd_print,  "print <expr>", "", "basic", CMD_NONE },
    { "set",    cmd_set,    "set $VAR = <expr>", "", "basic", CMD_NONE },
    { "unset",  cmd_unset,  "unset $VAR", "", "basic", CMD_NONE },
    { "wait",   cmd_wait,   "wait <time> | wait task <name> [timeout <t>]",
      "", "basic", CMD_NONE },
    { "reboot", cmd_reboot, "reboot", "", "basic", CMD_NONE },
    { "state",  cmd_state,  "state", "", "basic", CMD_REENTRANT },
    { "ps",     cmd_ps,     "ps [all|job|sys|name <n>|#tid|-a]", "",
      "task", CMD_REENTRANT },
    { "kill",   cmd_kill,   "kill <task|#tid>", "", "task", CMD_NONE },
    { "pause",  cmd_pause,  "pause <task|#tid>", "", "task", CMD_NONE },
    { "resume", cmd_resume, "resume <task|#tid>", "", "task", CMD_NONE },
    { "load",   cmd_load,   "load <file> [as <n>] [inherit]", "",
      "job", CMD_NONE },
    { "unload", cmd_unload, "unload <name>", "", "job", CMD_NONE },
    { "loads",  cmd_loads,  "loads", "", "job", CMD_NONE },
    { "run",    builtin_run,    "run [<file>] [&]", "", "job", CMD_NONE },
    { "start",  cmd_start,  "start <name>", "", "job", CMD_NONE },
    { "stop",   cmd_stop,   "stop <name> [force]", "", "job", CMD_NONE },
    { "fg",     cmd_fg,     "fg <name>", "", "job", CMD_NONE },
    { "jobs",   cmd_jobs,   "jobs", "", "job", CMD_NONE },
    { "job",    cmd_job,    "job <name>", "", "job", CMD_NONE },
    { "waitjob", cmd_waitjob, "waitjob <name> [timeout <t>]", "",
      "job", CMD_NONE }
};

static int builtin_help(shell_t *sh, int argc, char **argv)
{
    uint32_t i;

    (void)sh;
    if (argc >= 2) {
        const shell_cmd_t *cmd = shell_cmd_find(argv[1]);

        if (cmd != NULL) {
            ros_printf("%s\n", cmd->usage != NULL ? cmd->usage : cmd->name);
            if (cmd->help != NULL && cmd->help[0] != '\0') {
                ros_printf("  %s\n", cmd->help);
            }
            return SHELL_OK;
        }
        {
            const cmd_node_t *node = cmd_find_command(argv[1]);

            if (node != NULL) {
                char buf[160];

                if (cmd_help(node->def, buf, sizeof(buf)) > 0) {
                    ros_printf("%s", buf);
                }
                return SHELL_OK;
            }
        }
        ros_puts("help topics: ps script expr units errors device");
        return SHELL_OK;
    }

    ros_printf("Commands:\n");
    for (i = 0U; i < sizeof(s_builtin) / sizeof(s_builtin[0]); i++) {
        if ((s_builtin[i].flags & CMD_HIDDEN) == 0U) {
            ros_printf("  %-28s %s\n",
                         s_builtin[i].usage != NULL ? s_builtin[i].usage
                                                    : s_builtin[i].name,
                         s_builtin[i].group != NULL ? s_builtin[i].group : "");
        }
    }
    /* 全局命令（组件 init 注册）跟随其后列出 */
    {
        const cmd_node_t *node;

        for (node = cmd_first(); node != NULL; node = cmd_next(node)) {
            char buf[128];

            if (node->def != NULL &&
                cmd_usage(node->def, buf, sizeof(buf)) > 0) {
                ros_printf("  %-28s %s\n", buf,
                             (node->def->desc != NULL) ? node->def->desc : "");
            }
        }
    }
    return SHELL_OK;
}

/* ============================== 查找与分派 ============================== */

/* 仅查找内建命令；扩展命令由 cmd 全局命令表提供 */
const shell_cmd_t *shell_cmd_find(const char *name)
{
    uint32_t i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0U; i < sizeof(s_builtin) / sizeof(s_builtin[0]); i++) {
        if (shell_ieq(s_builtin[i].name,
                      (uint16_t)strlen(s_builtin[i].name), name) &&
            strlen(s_builtin[i].name) == strlen(name)) {
            return &s_builtin[i];
        }
    }
    return NULL;
}

/* 命令分派：优先内建命令，其次 cmd 全局命令表 */
int shell_cmd_dispatch(shell_t *sh, int argc, char **argv)
{
    const shell_cmd_t *cmd;
    int rc;

    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return -SHELL_ERR_PARAM;
    }

    cmd = shell_cmd_find(argv[0]);
    if (cmd != NULL) {
        if (sh == NULL) {
            return -SHELL_ERR_PARAM;
        }
        rc = cmd->handler(sh, argc, argv);
        return (rc < 0) ? rc : SHELL_OK;
    }

    rc = cmd_dispatch(argc, argv);
    if (rc == ARGS_ERR_UNKNOWN) {
        return -SHELL_ERR_NOT_FOUND;
    }
    return (rc < 0) ? rc : SHELL_OK;
}
