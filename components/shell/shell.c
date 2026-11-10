/*
 * shell.c - Shell 实例管理、输出、行编辑与输入分派
 *
 * 每个 shell_t 实例拥有独立的变量表 / JOB 表 / 输入缓冲 / 行编辑 /
 * 历史 / 粘贴缓冲 / 输出后端；默认实例由 shell_setup() 创建并绑定串口
 * 或标准输入输出，虚拟实例由 shell_instance_create() 创建。
 *
 * 输出：先格式化到栈缓冲，再在全局输出互斥内一次性写出，保证
 *       "[JOB X] ..." 整行原子，不会被其他任务/实例插入。
 * 输入：shell_instance_feed() 投递字节到实例环形缓冲；输入任务取出后
 *       由行编辑器处理（回显、退格、历史、Ctrl-C/Ctrl-D、Tab）。
 * 分派：SHELL_CTX 命令优先 → 脚本语句；块（IF/FOR/WHILE…）累积到粘贴
 *       缓冲，闭合后作为匿名 JOB 同步执行。
 */

#include "shell_internal.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------ 全局状态 ------------------------------ */

static shell_t *s_default;              /* 默认实例（平台绑定） */
static iot_mutex_t *s_out_mutex;        /* 全局输出互斥，所有实例共享 */

/* ------------------------------ 输出 ------------------------------ */

void shell_out(shell_t *sh, const char *data, uint32_t len)
{
    if (sh == NULL || sh->cfg.write == NULL || data == NULL || len == 0U) {
        return;
    }
    shell_out_lock();
    (void)sh->cfg.write(data, len, sh->cfg.user);
    shell_out_unlock();
}

void shell_putchar(shell_t *sh, char c)
{
    shell_out(sh, &c, 1U);
}

void shell_puts(shell_t *sh, const char *s)
{
    char buf[128];
    int n;

    if (s == NULL) {
        return;
    }
    n = snprintf(buf, sizeof(buf), "%s\n", s);
    if (n > 0) {
        shell_out(sh, buf,
                  (uint32_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf) - 1));
    }
}

static void shell_vprintf(shell_t *sh, const char *job, const char *fmt,
                          va_list ap)
{
    char body[256];
    char line[320];
    int n;

    n = vsnprintf(body, sizeof(body), fmt, ap);
    if (n < 0) {
        return;
    }
    if (job != NULL) {
        int m = snprintf(line, sizeof(line), "[JOB %s] %s", job, body);

        if (m > 0) {
            shell_out(sh, line, (uint32_t)((m < (int)sizeof(line))
                                           ? m : (int)sizeof(line) - 1));
        }
    } else {
        shell_out(sh, body, (uint32_t)((n < (int)sizeof(body))
                                       ? n : (int)sizeof(body) - 1));
    }
}

void shell_printf(shell_t *sh, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    shell_vprintf(sh, NULL, fmt, ap);
    va_end(ap);
}

void shell_printf_job(shell_t *sh, const char *job, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    shell_vprintf(sh, job, fmt, ap);
    va_end(ap);
}

void shell_report_error(shell_t *sh, const shell_err_t *err, const char *job)
{
    if (err == NULL) {
        return;
    }
    if (err->code == SHELL_ERR_PARSE) {
        if (job != NULL) {
            shell_printf(sh, "PARSE L%u: %s @JOB %s\n", (unsigned)err->line,
                         err->msg, job);
        } else {
            shell_printf(sh, "PARSE L%u: %s\n", (unsigned)err->line, err->msg);
        }
        return;
    }
    if (job != NULL) {
        shell_printf(sh, "ERR %d: %s @JOB %s L%u\n", err->code, err->msg, job,
                     (unsigned)err->line);
    } else {
        shell_printf(sh, "ERR %d: %s\n", err->code, err->msg);
    }
}

/* ------------------------------ 实例生命周期 ------------------------------ */

void shell_instance_reset(shell_t *sh)
{
    if (sh == NULL) {
        return;
    }
    shell_vars_init(&sh->vars);
    shell_jobs_clear(sh);

    memset(sh->rx, 0, SHELL_RX_MAX);
    memset(sh->line, 0, SHELL_LINE_MAX);
    memset(sh->hist, 0, (size_t)SHELL_HISTORY_MAX * SHELL_LINE_MAX);
    memset(sh->paste, 0, SHELL_PASTE_MAX);

    sh->rx_head = 0U;
    sh->rx_tail = 0U;
    sh->ctrlc = false;
    sh->line_len = 0U;
    sh->last_esc = false;
    sh->hist_count = 0U;
    sh->hist_pos = 0U;
    sh->paste_len = 0U;
    sh->block_depth = 0;
    sh->in_paste = false;
}

shell_t *shell_instance_create(const shell_config_t *cfg)
{
    shell_t *sh;

    if (cfg == NULL) {
        return NULL;
    }
    if (shell_out_init() != 0) {
        return NULL;
    }

    sh = (shell_t *)iot_mem_calloc(1U, sizeof(*sh));
    if (sh == NULL) {
        return NULL;
    }
    sh->cfg = *cfg;

    /* JOB 槽位只分配指针表，实际 shell_job_t 在加载时按需分配 */
    sh->jobs = (shell_job_t **)iot_mem_calloc(SHELL_JOB_MAX,
                                              sizeof(shell_job_t *));
    sh->rx = (char *)iot_mem_malloc(SHELL_RX_MAX);
    sh->line = (char *)iot_mem_malloc(SHELL_LINE_MAX);
    sh->hist = (char (*)[SHELL_LINE_MAX])iot_mem_calloc(SHELL_HISTORY_MAX,
                                                        SHELL_LINE_MAX);
    sh->paste = (char *)iot_mem_malloc(SHELL_PASTE_MAX);

    if (sh->jobs == NULL || sh->rx == NULL || sh->line == NULL ||
        sh->hist == NULL || sh->paste == NULL) {
        iot_mem_free(sh->jobs);
        iot_mem_free(sh->rx);
        iot_mem_free(sh->line);
        iot_mem_free(sh->hist);
        iot_mem_free(sh->paste);
        iot_mem_free(sh);
        return NULL;
    }

    shell_instance_reset(sh);
    return sh;
}

void shell_instance_destroy(shell_t *sh)
{
    if (sh == NULL) {
        return;
    }
    shell_jobs_clear(sh);
    shell_vars_release(&sh->vars);
    iot_mem_free(sh->jobs);
    iot_mem_free(sh->rx);
    iot_mem_free(sh->line);
    iot_mem_free(sh->hist);
    iot_mem_free(sh->paste);
    if (s_default == sh) {
        s_default = NULL;
    }
    iot_mem_free(sh);
}

int shell_setup(const shell_config_t *cfg)
{
    if (cfg == NULL || cfg->write == NULL) {
        return -1;
    }
    if (s_default == NULL) {
        s_default = shell_instance_create(cfg);
        if (s_default == NULL) {
            return -1;
        }
        shell_cmd_clear();
        return 0;
    }
    s_default->cfg = *cfg;
    shell_cmd_clear();
    shell_instance_reset(s_default);
    return 0;
}

shell_t *shell_default(void)
{
    return s_default;
}

void shell_reset(void)
{
    shell_instance_reset(s_default);
}

int shell_register_cmd(const shell_cmd_t *cmd)
{
    return shell_cmd_add(cmd);
}

/* ------------------------------ 输入缓冲 ------------------------------ */

int shell_instance_feed(shell_t *sh, const char *data, uint32_t len)
{
    uint32_t i;
    uint32_t written = 0U;

    if (sh == NULL || data == NULL) {
        return 0;
    }
    for (i = 0U; i < len; i++) {
        uint16_t next = (uint16_t)((sh->rx_head + 1U) % SHELL_RX_MAX);

        if (next == sh->rx_tail) {
            break;              /* 满：丢弃多余字节 */
        }
        sh->rx[sh->rx_head] = data[i];
        sh->rx_head = next;
        written++;
    }
    return (int)written;
}

static int shell_rx_pop(shell_t *sh)
{
    if (sh->rx_tail == sh->rx_head) {
        return -1;
    }
    {
        int c = (unsigned char)sh->rx[sh->rx_tail];

        sh->rx_tail = (uint16_t)((sh->rx_tail + 1U) % SHELL_RX_MAX);
        return c;
    }
}

uint32_t shell_instance_pending(shell_t *sh)
{
    if (sh == NULL) {
        return 0U;
    }
    return (uint32_t)((sh->rx_head + SHELL_RX_MAX - sh->rx_tail)
                      % SHELL_RX_MAX);
}

bool shell_ctrl_c_pending(shell_t *sh)
{
    return (sh != NULL) && sh->ctrlc;
}

void shell_clear_ctrl_c(shell_t *sh)
{
    if (sh != NULL) {
        sh->ctrlc = false;
    }
}

/* ------------------------------ 默认实例包装 ------------------------------ */

int shell_feed(const char *data, uint32_t len)
{
    return shell_instance_feed(s_default, data, len);
}

uint32_t shell_pending(void)
{
    return shell_instance_pending(s_default);
}

/* ------------------------------ 脚本关键字 ------------------------------ */

bool shell_is_block_start(const char *tok)
{
    return shell_ieq(tok, (uint16_t)strlen(tok), "IF") ||
           shell_ieq(tok, (uint16_t)strlen(tok), "FOR") ||
           shell_ieq(tok, (uint16_t)strlen(tok), "WHILE") ||
           shell_ieq(tok, (uint16_t)strlen(tok), "LOOP") ||
           shell_ieq(tok, (uint16_t)strlen(tok), "JOB") ||
           shell_ieq(tok, (uint16_t)strlen(tok), "ON");
}

bool shell_is_script_keyword(const char *tok)
{
    static const char *const kw[] = {
        "SET", "PRINT", "IF", "ELSE", "END", "FOR", "ENDFOR", "WHILE",
        "ENDWHILE", "LOOP", "ENDLOOP", "BREAK", "CONTINUE", "WAIT",
        "START", "STOP", "LABEL", "GOTO", "CALL", "RETURN", "JOB",
        "ENDJOB", "ON", "ENDERROR", "ENDSTOP", "SHELL"
    };
    uint32_t i;

    for (i = 0U; i < sizeof(kw) / sizeof(kw[0]); i++) {
        if (shell_ieq(tok, (uint16_t)strlen(tok), kw[i])) {
            return true;
        }
    }
    return false;
}

/* 更新块深度（用于续行判断） */
static void shell_depth_update(shell_t *sh, const char *tok)
{
    if (shell_ieq(tok, (uint16_t)strlen(tok), "IF") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "FOR") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "WHILE") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "LOOP") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "JOB") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "ON")) {
        sh->block_depth++;
    } else if (shell_ieq(tok, (uint16_t)strlen(tok), "END") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDFOR") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDWHILE") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDLOOP") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDJOB") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDERROR") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDSTOP")) {
        if (sh->block_depth > 0) {
            sh->block_depth--;
        }
    }
}

/* ------------------------------ 命令分派 ------------------------------ */

/* 把一行切成 argv（引号保留在 token 内，供表达式解析） */
static int shell_split(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < max) {
        bool quoted = false;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0') {
            if (*p == '"') {
                quoted = !quoted;
            } else if (!quoted && (*p == ' ' || *p == '\t')) {
                break;
            }
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return argc;
}

/* 去掉行首空白并返回首个 token（原地修改） */
static char *shell_first_token(char *line, char **rest)
{
    char *p = line;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    *rest = p;
    while (**rest != '\0' && **rest != ' ' && **rest != '\t') {
        (*rest)++;
    }
    if (**rest != '\0') {
        *(*rest)++ = '\0';
    }
    return p;
}

int shell_instance_exec_line(shell_t *sh, const char *line)
{
    char buf[SHELL_LINE_MAX];
    char *argv[8];
    char *tok;
    char *rest;
    int argc;
    const shell_cmd_t *cmd;

    if (sh == NULL || line == NULL) {
        return -SHELL_ERR_PARAM;
    }
    (void)snprintf(buf, sizeof(buf), "%s", line);

    tok = shell_first_token(buf, &rest);
    if (*tok == '\0') {
        return SHELL_OK;
    }

    /* SHELL_CTX：命令 → 脚本语句 */
    cmd = shell_cmd_find(tok);
    if (cmd != NULL) {
        char argbuf[SHELL_LINE_MAX];

        (void)snprintf(argbuf, sizeof(argbuf), "%s", line);
        argc = shell_split(argbuf, argv, 8);
        return shell_cmd_dispatch(sh, argc, argv);
    }

    if (shell_is_script_keyword(tok)) {
        return shell_instance_exec_script(sh, line);
    }

    shell_error(sh, SHELL_ERR_PARSE, "unknown command '%s'", tok);
    return -SHELL_ERR_PARSE;
}

int shell_instance_exec_script(shell_t *sh, const char *text)
{
    if (sh == NULL || text == NULL) {
        return -SHELL_ERR_PARAM;
    }
    return shell_job_run_sync(sh, text, "@interactive", true);
}

int shell_exec_line(const char *line)
{
    if (s_default == NULL) {
        return -SHELL_ERR_STATE;
    }
    return shell_instance_exec_line(s_default, line);
}

int shell_exec_script(const char *text)
{
    if (s_default == NULL) {
        return -SHELL_ERR_STATE;
    }
    return shell_instance_exec_script(s_default, text);
}

/* ------------------------------ 嵌套实例执行 ------------------------------ */

/* 子实例输出转发到父实例后端 */
static int shell_forward_write(const char *data, uint32_t len, void *user)
{
    shell_out((shell_t *)user, data, len);
    return (int)len;
}

int shell_exec_isolated(shell_t *parent, const char *text)
{
    shell_config_t cfg;
    shell_t *child;
    int rc;

    if (parent == NULL || text == NULL) {
        return -SHELL_ERR_PARAM;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.version = parent->cfg.version;
    cfg.write = shell_forward_write;
    cfg.user = parent;
    cfg.reset = parent->cfg.reset;

    child = shell_instance_create(&cfg);
    if (child == NULL) {
        return -SHELL_ERR_RESOURCE;
    }
    rc = shell_instance_exec_script(child, text);
    shell_instance_destroy(child);
    return rc;
}

/* ------------------------------ 粘贴缓冲 ------------------------------ */

const char *shell_paste_buffer(shell_t *sh, uint32_t *len)
{
    if (sh == NULL) {
        if (len != NULL) {
            *len = 0U;
        }
        return "";
    }
    sh->paste[sh->paste_len] = '\0';
    if (len != NULL) {
        *len = sh->paste_len;
    }
    return sh->paste;
}

int shell_run_paste_buffer(shell_t *sh)
{
    int rc;

    if (sh == NULL) {
        return -SHELL_ERR_PARAM;
    }
    if (sh->paste_len == 0U) {
        shell_puts(sh, "(empty)");
        return SHELL_OK;
    }
    sh->paste[sh->paste_len] = '\0';
    rc = shell_instance_exec_script(sh, sh->paste);
    sh->paste_len = 0U;
    sh->block_depth = 0;
    return rc;
}

/* ------------------------------ 行编辑 ------------------------------ */

static void shell_prompt(shell_t *sh)
{
    if (sh->block_depth > 0) {
        shell_out(sh, "...> ", 5U);
    } else {
        shell_out(sh, "> ", 2U);
    }
}

static void shell_history_add(shell_t *sh, const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }
    if (sh->hist_count > 0U &&
        strcmp(sh->hist[(sh->hist_count - 1U) % SHELL_HISTORY_MAX], line) == 0) {
        return;
    }
    (void)snprintf(sh->hist[sh->hist_count % SHELL_HISTORY_MAX],
                   SHELL_LINE_MAX, "%s", line);
    sh->hist_count++;
    sh->hist_pos = sh->hist_count;
}

static void shell_line_clear(shell_t *sh)
{
    uint16_t i;

    for (i = 0U; i < sh->line_len; i++) {
        shell_puts(sh, "\b \b");
    }
    sh->line_len = 0U;
    sh->line[0] = '\0';
}

/* 提交一行：块累积或直接执行 */
static void shell_submit_line(shell_t *sh)
{
    char tok_buf[SHELL_LINE_MAX];
    char *tok;
    char *rest;
    uint16_t len = sh->line_len;

    sh->line[sh->line_len] = '\0';
    shell_out(sh, "\n", 1U);

    if (sh->line_len == 0U) {
        shell_prompt(sh);
        return;
    }

    /* 单独一个 . 放弃当前块 */
    if (strcmp(sh->line, ".") == 0) {
        sh->paste_len = 0U;
        sh->block_depth = 0;
        sh->line_len = 0U;
        shell_prompt(sh);
        return;
    }

    shell_history_add(sh, sh->line);

    (void)snprintf(tok_buf, sizeof(tok_buf), "%s", sh->line);
    tok = shell_first_token(tok_buf, &rest);

    /* 续行态或块起始：累积到粘贴缓冲 */
    if (sh->block_depth > 0 || (tok[0] != '\0' && shell_is_block_start(tok))) {
        if ((uint32_t)sh->paste_len + len + 2U > SHELL_PASTE_MAX) {
            shell_error(sh, SHELL_ERR_RESOURCE, "paste buffer overflow");
            sh->paste_len = 0U;
            sh->block_depth = 0;
        } else {
            memcpy(&sh->paste[sh->paste_len], sh->line, len);
            sh->paste_len = (uint16_t)(sh->paste_len + len);
            sh->paste[sh->paste_len++] = '\n';
            shell_depth_update(sh, tok);
        }
        sh->line_len = 0U;
        if (sh->block_depth == 0 && sh->paste_len > 0U) {
            sh->paste[sh->paste_len] = '\0';
            (void)shell_instance_exec_script(sh, sh->paste);
            sh->paste_len = 0U;
        }
        shell_prompt(sh);
        return;
    }

    (void)shell_instance_exec_line(sh, sh->line);
    sh->line_len = 0U;
    shell_prompt(sh);
}

/* Tab 补全：只补命令名，多个候选时列出 */
static void shell_complete(shell_t *sh)
{
    static const char *const names[] = {
        "help", "ver", "echo", "print", "set", "unset", "wait", "reboot",
        "state", "ps", "kill", "pause", "resume", "load", "unload",
        "loads", "run", "start", "stop", "fg", "jobs", "job", "waitjob"
    };
    char prefix[32];
    uint16_t n = sh->line_len;
    uint32_t i;
    uint32_t matches = 0U;
    const char *last = NULL;

    if (n == 0U || n >= sizeof(prefix)) {
        return;
    }
    memcpy(prefix, sh->line, n);
    prefix[n] = '\0';

    for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strncmp(names[i], prefix, n) == 0) {
            matches++;
            last = names[i];
        }
    }
    if (matches == 1U && last != NULL) {
        const char *suffix = &last[n];

        while (*suffix != '\0' && sh->line_len + 1U < SHELL_LINE_MAX) {
            sh->line[sh->line_len++] = *suffix++;
            shell_putchar(sh, sh->line[sh->line_len - 1U]);
        }
        sh->line[sh->line_len] = '\0';
    } else if (matches > 1U) {
        shell_out(sh, "\n", 1U);
        for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
            if (strncmp(names[i], prefix, n) == 0) {
                shell_out(sh, names[i], (uint32_t)strlen(names[i]));
                shell_out(sh, " ", 1U);
            }
        }
        shell_out(sh, "\n", 1U);
        shell_prompt(sh);
        shell_out(sh, sh->line, sh->line_len);
    }
}

/* 处理一个输入字节 */
static void shell_feed_char(shell_t *sh, int c)
{
    if (c < 0) {
        return;
    }
    if (c == 0x03) {                    /* Ctrl-C */
        sh->ctrlc = true;
        sh->line_len = 0U;
        shell_puts(sh, "^C");
        shell_prompt(sh);
        return;
    }
    if (c == 0x04) {                    /* Ctrl-D：执行粘贴缓冲 */
        (void)shell_run_paste_buffer(sh);
        shell_prompt(sh);
        return;
    }
    if (c == '\r') {
        return;
    }
    if (c == '\n') {
        shell_submit_line(sh);
        return;
    }
    if (c == 0x08 || c == 0x7F) {
        if (sh->line_len > 0U) {
            sh->line_len--;
            sh->line[sh->line_len] = '\0';
            shell_out(sh, "\b \b", 3U);
        }
        return;
    }
    if (c == '\t') {
        shell_complete(sh);
        return;
    }
    if (c == 0x1B) {                    /* ESC 序列（历史） */
        sh->last_esc = true;
        return;
    }
    if (sh->last_esc && c == '[') {
        return;
    }
    if (sh->last_esc && (c == 'A' || c == 'B')) {
        sh->last_esc = false;
        if (sh->hist_count == 0U) {
            return;
        }
        if (c == 'A' && sh->hist_pos > 0U) {
            sh->hist_pos--;
        } else if (c == 'B' && sh->hist_pos + 1U < sh->hist_count) {
            sh->hist_pos++;
        }
        shell_line_clear(sh);
        if (sh->hist_pos < sh->hist_count) {
            const char *h = sh->hist[sh->hist_pos % SHELL_HISTORY_MAX];

            (void)snprintf(sh->line, SHELL_LINE_MAX, "%s", h);
            sh->line_len = (uint16_t)strlen(sh->line);
            shell_out(sh, sh->line, sh->line_len);
        }
        return;
    }
    sh->last_esc = false;

    if (c >= 0x20 && c < 0x7F && sh->line_len + 1U < SHELL_LINE_MAX) {
        sh->line[sh->line_len++] = (char)c;
        sh->line[sh->line_len] = '\0';
        shell_putchar(sh, (char)c);
    }
}

/* ------------------------------ Shell 任务 ------------------------------ */

static void shell_task_entry(void *arg)
{
    shell_t *sh = (shell_t *)arg;

    shell_prompt(sh);
    for (;;) {
        int c = shell_rx_pop(sh);

        if (c < 0) {
            iot_sleep_ms(10U);
            continue;
        }
        shell_feed_char(sh, c);
    }
}

int shell_instance_start(shell_t *sh)
{
    if (sh == NULL) {
        return -1;
    }
    if (sh->task != NULL) {
        return 0;
    }
    sh->task = iot_task_create_dynamic("shell", (uint8_t)SHELL_SHELL_PRIO,
                                       shell_task_entry, sh,
                                       (uint32_t)SHELL_SHELL_STACK);
    return (sh->task != NULL) ? 0 : -1;
}

int shell_start(void)
{
    if (s_default == NULL) {
        return -1;
    }
    return shell_instance_start(s_default);
}
