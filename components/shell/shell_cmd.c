/*
 * shell_cmd.c - Shell 命令（SHELL_CTX）
 *
 * 覆盖 V1.1 §2~§4：基础命令、RTOS 任务管理、JOB 生命周期。
 * 设备层命令（STEP/PWM/GPIO…）不在本次实现范围内。
 */

#include "shell_internal.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define SHELL_TAG "shell"
#define SHELL_EXT_MAX 8U

/* shell.c 提供的上下文 */
extern const char *g_shell_version;

/* shell.c 提供的粘贴缓冲执行入口 */
int shell_run_paste_buffer(void);
bool shell_ctrl_c_pending(void);
void shell_clear_ctrl_c(void);

static shell_cmd_t s_ext[SHELL_EXT_MAX];
static uint8_t     s_ext_count;

/* ============================== 输出辅助 ============================== */

void shell_error(int code, const char *fmt, ...)
{
    char msg[64];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) {
        msg[0] = '\0';
    }
    shell_printf("ERR %d: %s\n", code, msg);
}

static void shell_warn(const char *fmt, ...)
{
    char msg[64];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    shell_printf("WARN: %s\n", msg);
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

static iot_task_t *shell_find_task(const char *arg)
{
    iot_task_t *task;
    uint16_t tid = 0U;

    if (arg == NULL) {
        return NULL;
    }
    if (shell_parse_tid(arg, &tid)) {
        for (task = iot_task_first(); task != NULL; task = iot_task_next(task)) {
            if (task->tid == tid) {
                return task;
            }
        }
        return NULL;
    }
    for (task = iot_task_first(); task != NULL; task = iot_task_next(task)) {
        if (task->name != NULL &&
            shell_ieq(task->name, (uint16_t)strlen(task->name), arg) &&
            strlen(task->name) == strlen(arg)) {
            return task;
        }
    }
    return NULL;
}

static const char *shell_task_state_name(iot_task_state_e state)
{
    switch (state) {
    case IOT_TASK_READY:      return "READY";
    case IOT_TASK_RUNNING:    return "RUNNING";
    case IOT_TASK_BLOCKED:    return "BLOCKED";
    case IOT_TASK_DELAYED:    return "BLOCKED";
    case IOT_TASK_SUSPENDED:  return "SUSPENDED";
    case IOT_TASK_TERMINATED: return "DELETED";
    default:                  return "?";
    }
}

/* ============================== 基础命令 ============================== */

static int cmd_help(int argc, char **argv);

static int cmd_ver(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_printf("IoT Shell v1.1\n");
    shell_printf("Build: %s\n", g_shell_version != NULL ? g_shell_version : "-");
    shell_printf("RTOS: iot-frame / per-job task model\n");
    shell_printf("FS: %s | CPU stats: %s\n",
                 SHELL_HAS_FS ? "enabled" : "disabled",
                 SHELL_HAS_CPU_STATS ? "enabled" : "disabled");
    return SHELL_OK;
}

/* echo：逐个 token 做 $VAR 替换，引号内的内容原样输出，不求值 */
static int cmd_echo(int argc, char **argv)
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
            shell_putchar(' ');
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
                    shell_putchar('$');
                    i++;
                    continue;
                }
                if (shell_vars_get(&g_shell_vars, &tok[start],
                                   (uint16_t)(end - start), &val, &err) == 0) {
                    shell_value_format(&val, buf, sizeof(buf), &out_len);
                    shell_out(buf, out_len);
                } else {
                    shell_warn("undefined variable $%.*s", (int)(end - start),
                               &tok[start]);
                    shell_putchar('$');
                    shell_out(&tok[start], (uint32_t)(end - start));
                }
                i = end;
                continue;
            }
            shell_putchar(tok[i]);
            i++;
        }
    }
    shell_putchar('\n');
    return SHELL_OK;
}

static int cmd_print(int argc, char **argv)
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
    if (shell_expr_eval(toks, (uint16_t)n, &g_shell_vars, &val, &err) != 0) {
        shell_report_error(&err, NULL);
        return -err.code;
    }
    shell_value_format(&val, buf, sizeof(buf), &out_len);
    shell_printf("%.*s\n", (int)out_len, buf);
    return SHELL_OK;
}

static int cmd_set(int argc, char **argv)
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
        shell_error(SHELL_ERR_PARSE, "SET expects $VAR = expr");
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
        shell_error(SHELL_ERR_PARSE, "SET expects $VAR = expr");
        return -SHELL_ERR_PARSE;
    }
    memset(&err, 0, sizeof(err));
    if (shell_expr_eval(&toks[eq + 1], (uint16_t)(n - eq - 1),
                        &g_shell_vars, &val, &err) != 0) {
        shell_report_error(&err, NULL);
        return -err.code;
    }
    if (shell_vars_set(&g_shell_vars, toks[0].p + 1,
                       (uint16_t)(toks[0].len - 1U), &val, false,
                       &err) != 0) {
        shell_report_error(&err, NULL);
        return -err.code;
    }
    return SHELL_OK;
}

static int cmd_unset(int argc, char **argv)
{
    const char *name;

    if (argc < 2 || argv[1][0] != '$') {
        shell_error(SHELL_ERR_PARAM, "usage: unset $VAR");
        return -SHELL_ERR_PARAM;
    }
    name = &argv[1][1];
    if (shell_vars_unset(&g_shell_vars, name, (uint16_t)strlen(name)) != 0) {
        shell_error(SHELL_ERR_NOT_FOUND, "variable not found");
        return -SHELL_ERR_NOT_FOUND;
    }
    return SHELL_OK;
}

/* 可被 Ctrl-C 打断的毫秒等待 */
static bool shell_wait_ms(uint32_t ms)
{
    uint32_t elapsed = 0U;

    while (elapsed < ms) {
        uint32_t step = (ms - elapsed > 20U) ? 20U : (ms - elapsed);

        if (shell_ctrl_c_pending()) {
            shell_clear_ctrl_c();
            shell_puts("WARN: wait interrupted");
            return false;
        }
        iot_sleep_ms(step);
        elapsed += step;
    }
    return true;
}

static int cmd_wait(int argc, char **argv)
{
    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: wait <time> | wait task <name>");
        return -SHELL_ERR_PARAM;
    }
    if (strcmp(argv[1], "task") == 0) {
        iot_task_t *task;
        uint32_t timeout = SHELL_WAIT_DEFAULT_MS;
        uint32_t waited = 0U;
        int i;

        if (argc < 3) {
            shell_error(SHELL_ERR_PARAM, "usage: wait task <name> [timeout <t>]");
            return -SHELL_ERR_PARAM;
        }
        for (i = 3; i + 1 < argc; i += 2) {
            if (strcmp(argv[i], "timeout") == 0) {
                timeout = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            }
        }
        task = shell_find_task(argv[2]);
        if (task == NULL) {
            shell_error(SHELL_ERR_NOT_FOUND, "task %s not found", argv[2]);
            return -SHELL_ERR_NOT_FOUND;
        }
        if ((task->name != NULL && strcmp(task->name, "shell") == 0) ||
            task == iot_idle_task) {
            shell_error(SHELL_ERR_PARAM, "cannot wait on shell/idle");
            return -SHELL_ERR_PARAM;
        }
        while (task->state != IOT_TASK_TERMINATED) {
            if (timeout != 0U && waited >= timeout) {
                shell_error(SHELL_ERR_TIMEOUT, "wait task %s timeout", argv[2]);
                return -SHELL_ERR_TIMEOUT;
            }
            if (shell_ctrl_c_pending()) {
                shell_clear_ctrl_c();
                shell_puts("WARN: wait interrupted");
                return SHELL_OK;
            }
            iot_sleep_ms(10U);
            waited += 10U;
        }
        return SHELL_OK;
    }

    /* wait <time>：表达式 + 单位（默认 ms） */
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
        if (shell_expr_eval(toks, (uint16_t)n, &g_shell_vars, &val, &err) != 0 ||
            shell_value_to_int(&val, &ms, &err) != 0) {
            shell_error(SHELL_ERR_PARAM, "invalid argument for wait");
            return -SHELL_ERR_PARAM;
        }
        (void)shell_wait_ms((ms > 0) ? (uint32_t)ms : 0U);
    }
    return SHELL_OK;
}

static int cmd_state(int argc, char **argv)
{
    uint32_t ready = 0U;
    uint32_t running = 0U;
    uint32_t blocked = 0U;
    uint32_t suspended = 0U;
    uint32_t total = 0U;
    iot_task_t *task;
    uint16_t i;

    (void)argc;
    (void)argv;

    for (task = iot_task_first(); task != NULL; task = iot_task_next(task)) {
        if (task->state == IOT_TASK_TERMINATED) {
            continue;
        }
        total++;
        switch (task->state) {
        case IOT_TASK_READY:     ready++; break;
        case IOT_TASK_RUNNING:   running++; break;
        case IOT_TASK_BLOCKED:
        case IOT_TASK_DELAYED:   blocked++; break;
        case IOT_TASK_SUSPENDED: suspended++; break;
        default: break;
        }
    }
    shell_printf("RTOS Tasks: %u (RUNNING=%u READY=%u BLOCKED=%u SUSPENDED=%u)\n",
                 (unsigned)total, (unsigned)running, (unsigned)ready,
                 (unsigned)blocked, (unsigned)suspended);
    shell_printf("JOBs      :");
    {
        bool any = false;

        for (i = 0U; i < SHELL_JOB_MAX; i++) {
            shell_job_t *job = shell_job_slot(i);

            if (job != NULL) {
                any = true;
                shell_printf(" %s=%s", job->name,
                             shell_job_state_name(job->state));
            }
        }
        if (!any) {
            shell_printf(" (none)");
        }
    }
    shell_printf("\n");
    shell_printf("Shell Vars:");
    for (i = 0U; i < SHELL_VAR_MAX; i++) {
        const shell_var_t *v = &g_shell_vars.items[i];

        if (v->used) {
            shell_printf(" %s=", v->name);
            if (v->type == SHELL_VAL_STRING) {
                shell_printf("%s", v->str);
            } else if (v->type == SHELL_VAL_INT) {
                shell_printf("%ld", (long)v->i);
            } else if (v->type == SHELL_VAL_FLOAT) {
                shell_printf("%g", (double)v->f);
            } else {
                shell_printf("%s", v->b ? "TRUE" : "FALSE");
            }
        }
    }
    shell_printf("\n");
    return SHELL_OK;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (g_shell_reset == NULL) {
        shell_error(SHELL_ERR_PARAM, "reset not supported");
        return -SHELL_ERR_PARAM;
    }
    shell_puts("rebooting...");
    g_shell_reset();
    return SHELL_OK;
}

/* ------------------------------ ps ------------------------------ */

static void shell_ps_line(const iot_task_t *task)
{
    shell_job_t *job = shell_job_find_by_task(task);
    const char *type = (job != NULL) ? "JOB" : "SYS";
    const char *state = shell_task_state_name(task->state);
    char cpu[8] = "-";
    char stack[8];

    (void)snprintf(stack, sizeof(stack), "%u", (unsigned)task->stack_size);
#if SHELL_HAS_CPU_STATS
    (void)snprintf(cpu, sizeof(cpu), "%.1f", 0.0);
#endif
    shell_printf("%3u  %-8s  %-4s  %-9s  %4u  %6s  %s  %s\n",
                 (unsigned)task->tid,
                 task->name != NULL ? task->name : "?",
                 type, state, (unsigned)task->priority, stack, cpu,
                 job != NULL ? job->name : "-");
}

static int cmd_ps(int argc, char **argv)
{
    iot_task_t *task;
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

    shell_printf("TID  NAME      TYPE   STATE      PRIO  STACK   CPU%%   JOB\n");
    shell_printf("---  --------  -----  ---------  ----  ------  -----  ----\n");
    for (task = iot_task_first(); task != NULL; task = iot_task_next(task)) {
        shell_job_t *job = shell_job_find_by_task(task);

        if (!show_all && task->state == IOT_TASK_TERMINATED) {
            continue;
        }
        if (only_job && job == NULL) {
            continue;
        }
        if (only_sys && job != NULL) {
            continue;
        }
        shell_ps_line(task);
    }
    return SHELL_OK;
}

/* ------------------------------ kill / pause / resume ------------------------------ */

static bool shell_task_is_protected(const iot_task_t *task)
{
    return (task == iot_current_task) || (task == iot_idle_task) ||
           (task->name != NULL && strcmp(task->name, "shell") == 0);
}

static int cmd_kill(int argc, char **argv)
{
    iot_task_t *task;
    shell_job_t *job;

    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: kill <task|#tid> [confirm]");
        return -SHELL_ERR_PARAM;
    }
    task = shell_find_task(argv[1]);
    if (task == NULL) {
        shell_error(SHELL_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (shell_task_is_protected(task)) {
        shell_error(SHELL_ERR_DANGER, "refusing to kill shell/idle");
        return -SHELL_ERR_DANGER;
    }
    job = shell_job_find_by_task(task);
    if (job != NULL) {
        (void)shell_job_stop(job, true);
        job->state = JOB_ABORTED;
        return SHELL_OK;
    }
    /* 系统任务：必须有 confirm（由 dispatcher 统一校验），此处直接删除 */
    if (iot_task_destroy(task) != IOT_OK) {
        shell_error(SHELL_ERR_STATE, "task busy or not destroyable");
        return -SHELL_ERR_STATE;
    }
    shell_warn("system task deleted");
    return SHELL_OK;
}

static int cmd_pause(int argc, char **argv)
{
    iot_task_t *task;
    shell_job_t *job;

    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: pause <task|#tid> [confirm]");
        return -SHELL_ERR_PARAM;
    }
    task = shell_find_task(argv[1]);
    if (task == NULL) {
        shell_error(SHELL_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (shell_task_is_protected(task)) {
        shell_error(SHELL_ERR_DANGER, "refusing to pause shell/idle");
        return -SHELL_ERR_DANGER;
    }
    if (iot_task_suspend(task) != IOT_OK) {
        shell_error(SHELL_ERR_STATE, "task cannot be paused");
        return -SHELL_ERR_STATE;
    }
    job = shell_job_find_by_task(task);
    if (job != NULL) {
        job->state = JOB_PAUSED;
    }
    return SHELL_OK;
}

static int cmd_resume(int argc, char **argv)
{
    iot_task_t *task;
    shell_job_t *job;

    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: resume <task|#tid>");
        return -SHELL_ERR_PARAM;
    }
    task = shell_find_task(argv[1]);
    if (task == NULL) {
        shell_error(SHELL_ERR_NOT_FOUND, "task %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (iot_task_resume(task) != IOT_OK) {
        shell_error(SHELL_ERR_STATE, "task is not suspended");
        return -SHELL_ERR_STATE;
    }
    job = shell_job_find_by_task(task);
    if (job != NULL) {
        job->state = JOB_RUNNING;
    }
    return SHELL_OK;
}

/* ------------------------------ JOB 管理 ------------------------------ */

static int cmd_load(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if SHELL_HAS_FS
    shell_error(SHELL_ERR_NOT_FOUND, "load not implemented");
#else
    shell_error(SHELL_ERR_NOT_FOUND, "file system not available");
#endif
    return -SHELL_ERR_NOT_FOUND;
}

static int cmd_unload(int argc, char **argv)
{
    shell_job_t *job;

    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: unload <name>");
        return -SHELL_ERR_PARAM;
    }
    job = shell_job_find(argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (job->state == JOB_RUNNING || job->state == JOB_PAUSED ||
        job->state == JOB_STOPPING) {
        shell_error(SHELL_ERR_STATE, "job %s is %s", argv[1],
                    shell_job_state_name(job->state));
        return -SHELL_ERR_STATE;
    }
    shell_job_unload(job);
    return SHELL_OK;
}

static int cmd_loads(int argc, char **argv)
{
    uint16_t i;
    bool any = false;

    (void)argc;
    (void)argv;
    shell_printf("NAME   STATE      SCRIPT\n");
    for (i = 0U; i < SHELL_JOB_MAX; i++) {
        shell_job_t *job = shell_job_slot(i);

        if (job != NULL) {
            any = true;
            shell_printf("%-6s %-10s %s\n", job->name,
                         shell_job_state_name(job->state),
                         job->text_len > 0U ? "(inline)" : "-");
        }
    }
    if (!any) {
        shell_puts("(no jobs)");
    }
    return SHELL_OK;
}

static int cmd_jobs(int argc, char **argv)
{
    uint16_t i;
    bool any = false;

    (void)argc;
    (void)argv;
    shell_printf("NAME   TASK   TID  STATE      PC\n");
    for (i = 0U; i < SHELL_JOB_MAX; i++) {
        shell_job_t *job = shell_job_slot(i);

        if (job == NULL) {
            continue;
        }
        if (job->state == JOB_RUNNING || job->state == JOB_PAUSED ||
            job->state == JOB_STOPPING) {
            any = true;
            shell_printf("%-6s %-6s %3u  %-10s L%u\n", job->name, job->name,
                         (unsigned)(job->task != NULL ? job->task->tid : 0U),
                         shell_job_state_name(job->state), (unsigned)job->pc);
        }
    }
    if (!any) {
        shell_puts("(no jobs)");
    }
    return SHELL_OK;
}

static int cmd_job(int argc, char **argv)
{
    shell_job_t *job;
    uint16_t i;

    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: job <name>");
        return -SHELL_ERR_PARAM;
    }
    job = shell_job_find(argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    shell_printf("Name    : %s\n", job->name);
    shell_printf("JobID   : %u\n", (unsigned)job->job_id);
    shell_printf("TID     : %u\n",
                 (unsigned)(job->task != NULL ? job->task->tid : 0U));
    shell_printf("State   : %s\n", shell_job_state_name(job->state));
    shell_printf("Prio    : %u\n", (unsigned)job->prio);
    shell_printf("Stack   : %u\n", (unsigned)job->stack);
    shell_printf("PC      : L%u\n", (unsigned)job->pc);
    shell_printf("Vars    :");
    for (i = 0U; i < SHELL_VAR_MAX; i++) {
        if (job->vars.items[i].used) {
            shell_printf(" $%s", job->vars.items[i].name);
        }
    }
    shell_printf("\n");
    return SHELL_OK;
}

static int shell_job_cmd_start(const char *name)
{
    shell_job_t *job = shell_job_find(name, (uint16_t)strlen(name));

    if (job == NULL) {
        shell_error(SHELL_ERR_NOT_FOUND, "job %s not found", name);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (job->state == JOB_RUNNING || job->state == JOB_PAUSED) {
        shell_error(SHELL_ERR_STATE, "job %s is %s", name,
                    shell_job_state_name(job->state));
        return -SHELL_ERR_STATE;
    }
    if (shell_job_start(job) != 0) {
        shell_error(SHELL_ERR_STATE, "job %s is not LOADED", name);
        return -SHELL_ERR_STATE;
    }
    return SHELL_OK;
}

static int cmd_start(int argc, char **argv)
{
    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: start <name>");
        return -SHELL_ERR_PARAM;
    }
    return shell_job_cmd_start(argv[1]);
}

static int cmd_stop(int argc, char **argv)
{
    shell_job_t *job;
    bool force = false;
    int i;

    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: stop <name> [force]");
        return -SHELL_ERR_PARAM;
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "force") == 0) {
            force = true;
        }
    }
    job = shell_job_find(argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (shell_job_stop(job, force) != 0) {
        shell_error(SHELL_ERR_STATE, "job %s is not running", argv[1]);
        return -SHELL_ERR_STATE;
    }
    return SHELL_OK;
}

static int cmd_fg(int argc, char **argv)
{
    shell_job_t *job;
    uint32_t timeout = 0U;

    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: fg <name>");
        return -SHELL_ERR_PARAM;
    }
    job = shell_job_find(argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    job->foreground = true;
    if (job->state == JOB_LOADED) {
        if (shell_job_start(job) != 0) {
            shell_error(SHELL_ERR_STATE, "job %s start failed", argv[1]);
            return -SHELL_ERR_STATE;
        }
    }
    if (shell_job_wait(job, timeout) != 0) {
        shell_error(SHELL_ERR_TIMEOUT, "fg %s timeout", argv[1]);
        return -SHELL_ERR_TIMEOUT;
    }
    return SHELL_OK;
}

static int cmd_waitjob(int argc, char **argv)
{
    shell_job_t *job;
    uint32_t timeout = SHELL_WAIT_DEFAULT_MS;
    int i;

    if (argc < 2) {
        shell_error(SHELL_ERR_PARAM, "usage: waitjob <name> [timeout <t>]");
        return -SHELL_ERR_PARAM;
    }
    for (i = 2; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "timeout") == 0) {
            timeout = (uint32_t)strtoul(argv[i + 1], NULL, 10);
        }
    }
    job = shell_job_find(argv[1], (uint16_t)strlen(argv[1]));
    if (job == NULL) {
        shell_error(SHELL_ERR_NOT_FOUND, "job %s not found", argv[1]);
        return -SHELL_ERR_NOT_FOUND;
    }
    if (job->state != JOB_RUNNING && job->state != JOB_PAUSED &&
        job->state != JOB_STOPPING) {
        return SHELL_OK;        /* 已结束：立即返回 */
    }
    if (shell_job_wait(job, timeout) != 0) {
        shell_error(SHELL_ERR_TIMEOUT, "waitjob %s timeout", argv[1]);
        return -SHELL_ERR_TIMEOUT;
    }
    return SHELL_OK;
}

/* run：前台执行一段脚本（FS 关闭时只支持粘贴缓冲） */
static int cmd_run(int argc, char **argv)
{
    bool background = false;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "&") == 0) {
            background = true;
        }
    }
    (void)background;

    if (argc < 2 || argv[1][0] == '&') {
        return shell_run_paste_buffer();
    }
#if !SHELL_HAS_FS
    shell_error(SHELL_ERR_NOT_FOUND, "file system not available");
    return -SHELL_ERR_NOT_FOUND;
#else
    shell_error(SHELL_ERR_NOT_FOUND, "run <file> not implemented");
    return -SHELL_ERR_NOT_FOUND;
#endif
}

/* ------------------------------ help ------------------------------ */

static const shell_cmd_t s_builtin[] = {
    { "help",   cmd_help,   "help [topic]", "", "basic", CMD_NONE },
    { "ver",    cmd_ver,    "ver", "", "basic", CMD_REENTRANT },
    { "echo",   cmd_echo,   "echo <text>", "", "basic", CMD_NONE },
    { "print",  cmd_print,  "print <expr>", "", "basic", CMD_NONE },
    { "set",    cmd_set,    "set $VAR = <expr>", "", "basic", CMD_NONE },
    { "unset",  cmd_unset,  "unset $VAR", "", "basic", CMD_NONE },
    { "wait",   cmd_wait,   "wait <time> | wait task <name> [timeout <t>]",
      "", "basic", CMD_NONE },
    { "reboot", cmd_reboot, "reboot confirm", "", "basic", CMD_DANGER },
    { "state",  cmd_state,  "state", "", "basic", CMD_REENTRANT },
    { "ps",     cmd_ps,     "ps [all|job|sys|name <n>|#tid|-a]", "",
      "task", CMD_REENTRANT },
    { "kill",   cmd_kill,   "kill <task|#tid> [confirm]", "", "task", CMD_NONE },
    { "pause",  cmd_pause,  "pause <task|#tid> [confirm]", "", "task", CMD_NONE },
    { "resume", cmd_resume, "resume <task|#tid>", "", "task", CMD_NONE },
    { "load",   cmd_load,   "load <file> [as <n>] [inherit]", "",
      "job", CMD_NONE },
    { "unload", cmd_unload, "unload <name>", "", "job", CMD_NONE },
    { "loads",  cmd_loads,  "loads", "", "job", CMD_NONE },
    { "run",    cmd_run,    "run [<file>] [&]", "", "job", CMD_NONE },
    { "start",  cmd_start,  "start <name>", "", "job", CMD_NONE },
    { "stop",   cmd_stop,   "stop <name> [force]", "", "job", CMD_NONE },
    { "fg",     cmd_fg,     "fg <name>", "", "job", CMD_NONE },
    { "jobs",   cmd_jobs,   "jobs", "", "job", CMD_NONE },
    { "job",    cmd_job,    "job <name>", "", "job", CMD_NONE },
    { "waitjob", cmd_waitjob, "waitjob <name> [timeout <t>]", "",
      "job", CMD_NONE }
};

static int cmd_help(int argc, char **argv)
{
    uint32_t i;

    if (argc >= 2) {
        const shell_cmd_t *cmd = shell_cmd_find(argv[1]);

        if (cmd != NULL) {
            shell_printf("%s\n", cmd->usage != NULL ? cmd->usage : cmd->name);
            if (cmd->help != NULL && cmd->help[0] != '\0') {
                shell_printf("  %s\n", cmd->help);
            }
            return SHELL_OK;
        }
        shell_puts("help topics: ps script expr units errors");
        return SHELL_OK;
    }

    shell_printf("Commands:\n");
    for (i = 0U; i < sizeof(s_builtin) / sizeof(s_builtin[0]); i++) {
        if ((s_builtin[i].flags & CMD_HIDDEN) == 0U) {
            shell_printf("  %-28s %s\n",
                         s_builtin[i].usage != NULL ? s_builtin[i].usage
                                                    : s_builtin[i].name,
                         s_builtin[i].group != NULL ? s_builtin[i].group : "");
        }
    }
    return SHELL_OK;
}

/* ============================== 注册与分派 ============================== */

const shell_cmd_t *shell_cmd_find(const char *name)
{
    uint32_t i;
    uint8_t k;

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
    for (k = 0U; k < s_ext_count; k++) {
        if (shell_ieq(s_ext[k].name, (uint16_t)strlen(s_ext[k].name), name) &&
            strlen(s_ext[k].name) == strlen(name)) {
            return &s_ext[k];
        }
    }
    return NULL;
}

int shell_cmd_add(const shell_cmd_t *cmd)
{
    if (cmd == NULL || cmd->name == NULL || cmd->handler == NULL) {
        return -1;
    }
    if (shell_cmd_find(cmd->name) != NULL) {
        return -1;      /* 基础命令优先，重名拒绝 */
    }
    if (s_ext_count >= SHELL_EXT_MAX) {
        return -1;
    }
    s_ext[s_ext_count++] = *cmd;
    return 0;
}

void shell_cmd_clear(void)
{
    s_ext_count = 0U;
}

/* 危险命令 / confirm 统一处理 */ 
int shell_cmd_dispatch(int argc, char **argv)
{
    const shell_cmd_t *cmd;
    bool confirm = false;
    int i;
    int rc;

    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return -SHELL_ERR_PARAM;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "confirm") == 0) {
            confirm = true;
        }
    }

    cmd = shell_cmd_find(argv[0]);
    if (cmd == NULL) {
        return -SHELL_ERR_NOT_FOUND;
    }

    if ((cmd->flags & CMD_DANGER) != 0U && !confirm) {
        shell_error(SHELL_ERR_DANGER, "dangerous command requires confirm");
        return -SHELL_ERR_DANGER;
    }
    if ((cmd->flags & CMD_DANGER) == 0U && confirm) {
        shell_warn("confirm ignored");
    }

    rc = cmd->handler(argc, argv);
    return (rc < 0) ? rc : SHELL_OK;
}
