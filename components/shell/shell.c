/*
 * shell.c - Shell 初始化、输出、行编辑与输入分派
 *
 * 输出：所有打印先格式化到栈缓冲，再在互斥锁内一次性写出，保证
 *       "[JOB X] ..." 整行原子，不会被其他任务插入。
 * 输入：shell_input() 投递字节到环形缓冲；shell 任务取出后由行编辑器
 *       处理（回显、退格、历史、Ctrl-C/Ctrl-D、Tab）。
 * 分派：SHELL_CTX 命令优先 → 脚本语句；块（IF/FOR/WHILE…）累积到粘贴
 *       缓冲，闭合后作为匿名 JOB 同步执行。
 */

#include "shell_internal.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define SHELL_TAG "shell"

const char *g_shell_version;
void (*g_shell_reset)(void);

static int (*s_write)(const char *data, uint32_t len, void *user);
static void *s_write_user;

/* ------------------------------ 输入缓冲 ------------------------------ */

static char     s_rx[256];
static uint16_t s_rx_head;
static uint16_t s_rx_tail;
static volatile bool s_ctrlc;

/* 当前行 */
static char     s_line[SHELL_LINE_MAX];
static uint16_t s_line_len;
static bool     s_last_esc;

/* 历史 */
static char     s_hist[SHELL_HISTORY_MAX][SHELL_LINE_MAX];
static uint16_t s_hist_count;
static uint16_t s_hist_pos;

/* 块 / 粘贴缓冲 */
static char     s_paste[SHELL_PASTE_MAX];
static uint16_t s_paste_len;
static int      s_block_depth;
static bool     s_in_paste;

/* ------------------------------ 输出 ------------------------------ */

void shell_out(const char *data, uint32_t len)
{
    if (s_write == NULL || data == NULL || len == 0U) {
        return;
    }
    shell_out_lock();
    (void)s_write(data, len, s_write_user);
    shell_out_unlock();
}

void shell_putchar(char c)
{
    shell_out(&c, 1U);
}

void shell_puts(const char *s)
{
    char buf[128];
    int n;

    if (s == NULL) {
        return;
    }
    n = snprintf(buf, sizeof(buf), "%s\n", s);
    if (n > 0) {
        shell_out(buf, (uint32_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf) - 1));
    }
}

static void shell_vprintf(const char *job, const char *fmt, va_list ap)
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
            shell_out(line, (uint32_t)((m < (int)sizeof(line)) ? m
                                                                : (int)sizeof(line) - 1));
        }
    } else {
        shell_out(body, (uint32_t)((n < (int)sizeof(body)) ? n
                                                           : (int)sizeof(body) - 1));
    }
}

void shell_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    shell_vprintf(NULL, fmt, ap);
    va_end(ap);
}

void shell_printf_job(const char *job, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    shell_vprintf(job, fmt, ap);
    va_end(ap);
}

void shell_report_error(const shell_err_t *err, const char *job)
{
    if (err == NULL) {
        return;
    }
    if (err->code == SHELL_ERR_PARSE) {
        if (job != NULL) {
            shell_printf("PARSE L%u: %s @JOB %s\n", (unsigned)err->line,
                         err->msg, job);
        } else {
            shell_printf("PARSE L%u: %s\n", (unsigned)err->line, err->msg);
        }
        return;
    }
    if (job != NULL) {
        shell_printf("ERR %d: %s @JOB %s L%u\n", err->code, err->msg, job,
                     (unsigned)err->line);
    } else {
        shell_printf("ERR %d: %s\n", err->code, err->msg);
    }
}

/* ------------------------------ 初始化 ------------------------------ */

int shell_setup(const shell_config_t *cfg)
{
    if (cfg == NULL || cfg->write == NULL) {
        return -1;
    }

    g_shell_version = cfg->version;
    g_shell_reset = cfg->reset;
    s_write = cfg->write;
    s_write_user = cfg->user;

    shell_vars_init(&g_shell_vars);
    shell_cmd_clear();
    shell_jobs_clear();
    (void)shell_out_init();

    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_ctrlc = false;
    s_line_len = 0U;
    s_last_esc = false;
    s_hist_count = 0U;
    s_hist_pos = 0U;
    s_paste_len = 0U;
    s_block_depth = 0;
    s_in_paste = false;
    return 0;
}

void shell_reset(void)
{
    shell_vars_init(&g_shell_vars);
    shell_jobs_clear();
    s_line_len = 0U;
    s_hist_count = 0U;
    s_hist_pos = 0U;
    s_paste_len = 0U;
    s_block_depth = 0;
    s_in_paste = false;
}

int shell_register_cmd(const shell_cmd_t *cmd)
{
    return shell_cmd_add(cmd);
}

/* ------------------------------ 输入缓冲 ------------------------------ */

int shell_feed(const char *data, uint32_t len)
{
    uint32_t i;
    uint32_t written = 0U;

    if (data == NULL) {
        return 0;
    }
    for (i = 0U; i < len; i++) {
        uint16_t next = (uint16_t)((s_rx_head + 1U) % sizeof(s_rx));

        if (next == s_rx_tail) {
            break;              /* 满：丢弃多余字节 */
        }
        s_rx[s_rx_head] = data[i];
        s_rx_head = next;
        written++;
    }
    return (int)written;
}

static int shell_rx_pop(void)
{
    if (s_rx_tail == s_rx_head) {
        return -1;
    }
    {
        int c = (unsigned char)s_rx[s_rx_tail];

        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % sizeof(s_rx));
        return c;
    }
}

uint32_t shell_pending(void)
{
    return (uint32_t)((s_rx_head + sizeof(s_rx) - s_rx_tail) % sizeof(s_rx));
}

bool shell_ctrl_c_pending(void)
{
    return s_ctrlc;
}

void shell_clear_ctrl_c(void)
{
    s_ctrlc = false;
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
static void shell_depth_update(const char *tok)
{
    if (shell_ieq(tok, (uint16_t)strlen(tok), "IF") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "FOR") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "WHILE") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "LOOP") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "JOB") ||
        shell_ieq(tok, (uint16_t)strlen(tok), "ON")) {
        s_block_depth++;
    } else if (shell_ieq(tok, (uint16_t)strlen(tok), "END") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDFOR") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDWHILE") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDLOOP") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDJOB") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDERROR") ||
               shell_ieq(tok, (uint16_t)strlen(tok), "ENDSTOP")) {
        if (s_block_depth > 0) {
            s_block_depth--;
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

int shell_exec_line(const char *line)
{
    char buf[SHELL_LINE_MAX];
    char *argv[8];
    char *tok;
    char *rest;
    int argc;
    const shell_cmd_t *cmd;

    if (line == NULL) {
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
        return shell_cmd_dispatch(argc, argv);
    }

    if (shell_is_script_keyword(tok)) {
        return shell_exec_script(line);
    }

    shell_error(SHELL_ERR_PARSE, "unknown command '%s'", tok);
    return -SHELL_ERR_PARSE;
}

int shell_exec_script(const char *text)
{
    if (text == NULL) {
        return -SHELL_ERR_PARAM;
    }
    return shell_job_run_sync(text, "@interactive", true);
}

int shell_run_paste_buffer(void)
{
    int rc;

    if (s_paste_len == 0U) {
        shell_puts("(empty)");
        return SHELL_OK;
    }
    s_paste[s_paste_len] = '\0';
    rc = shell_exec_script(s_paste);
    s_paste_len = 0U;
    s_block_depth = 0;
    return rc;
}

/* ------------------------------ 行编辑 ------------------------------ */

static void shell_prompt(void)
{
    if (s_block_depth > 0) {
        shell_out("...> ", 5U);
    } else {
        shell_out("> ", 2U);
    }
}

static void shell_history_add(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }
    if (s_hist_count > 0U &&
        strcmp(s_hist[(s_hist_count - 1U) % SHELL_HISTORY_MAX], line) == 0) {
        return;
    }
    (void)snprintf(s_hist[s_hist_count % SHELL_HISTORY_MAX], SHELL_LINE_MAX,
                   "%s", line);
    s_hist_count++;
    s_hist_pos = s_hist_count;
}

static void shell_line_clear(void)
{
    uint16_t i;

    for (i = 0U; i < s_line_len; i++) {
        shell_puts("\b \b");
    }
    s_line_len = 0U;
    s_line[0] = '\0';
}

/* 提交一行：块累积或直接执行 */
static void shell_submit_line(void)
{
    char tok_buf[SHELL_LINE_MAX];
    char *tok;
    char *rest;
    uint16_t len = s_line_len;

    s_line[s_line_len] = '\0';
    shell_out("\n", 1U);

    if (s_line_len == 0U) {
        s_line_len = 0U;
        shell_prompt();
        return;
    }

    /* 单独一个 . 放弃当前块 */
    if (strcmp(s_line, ".") == 0) {
        s_paste_len = 0U;
        s_block_depth = 0;
        s_line_len = 0U;
        shell_prompt();
        return;
    }

    shell_history_add(s_line);

    (void)snprintf(tok_buf, sizeof(tok_buf), "%s", s_line);
    tok = shell_first_token(tok_buf, &rest);

    /* 续行态或块起始：累积到粘贴缓冲 */
    if (s_block_depth > 0 || (tok[0] != '\0' && shell_is_block_start(tok))) {
        if ((uint32_t)s_paste_len + len + 2U > SHELL_PASTE_MAX) {
            shell_error(SHELL_ERR_RESOURCE, "paste buffer overflow");
            s_paste_len = 0U;
            s_block_depth = 0;
        } else {
            memcpy(&s_paste[s_paste_len], s_line, len);
            s_paste_len = (uint16_t)(s_paste_len + len);
            s_paste[s_paste_len++] = '\n';
            shell_depth_update(tok);
        }
        s_line_len = 0U;
        if (s_block_depth == 0 && s_paste_len > 0U) {
            s_paste[s_paste_len] = '\0';
            (void)shell_exec_script(s_paste);
            s_paste_len = 0U;
        }
        shell_prompt();
        return;
    }

    (void)shell_exec_line(s_line);
    s_line_len = 0U;
    shell_prompt();
}

/* Tab 补全：只补命令名，多个候选时列出 */
static void shell_complete(void)
{
    static const char *const names[] = {
        "help", "ver", "echo", "print", "set", "unset", "wait", "reboot",
        "state", "ps", "kill", "pause", "resume", "load", "unload",
        "loads", "run", "start", "stop", "fg", "jobs", "job", "waitjob"
    };
    char prefix[32];
    uint16_t n = s_line_len;
    uint32_t i;
    uint32_t matches = 0U;
    const char *last = NULL;

    if (n == 0U || n >= sizeof(prefix)) {
        return;
    }
    memcpy(prefix, s_line, n);
    prefix[n] = '\0';

    for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strncmp(names[i], prefix, n) == 0) {
            matches++;
            last = names[i];
        }
    }
    if (matches == 1U && last != NULL) {
        const char *suffix = &last[n];

        while (*suffix != '\0' && s_line_len + 1U < SHELL_LINE_MAX) {
            s_line[s_line_len++] = *suffix++;
            shell_putchar(s_line[s_line_len - 1U]);
        }
        s_line[s_line_len] = '\0';
    } else if (matches > 1U) {
        shell_out("\n", 1U);
        for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
            if (strncmp(names[i], prefix, n) == 0) {
                shell_out(names[i], (uint32_t)strlen(names[i]));
                shell_out(" ", 1U);
            }
        }
        shell_out("\n", 1U);
        shell_prompt();
        shell_out(s_line, s_line_len);
    }
}

/* 处理一个输入字节 */
static void shell_feed_char(int c)
{
    if (c < 0) {
        return;
    }
    if (c == 0x03) {                    /* Ctrl-C */
        s_ctrlc = true;
        s_line_len = 0U;
        shell_puts("^C");
        shell_prompt();
        return;
    }
    if (c == 0x04) {                    /* Ctrl-D：执行粘贴缓冲 */
        (void)shell_run_paste_buffer();
        shell_prompt();
        return;
    }
    if (c == '\r') {
        return;
    }
    if (c == '\n') {
        shell_submit_line();
        return;
    }
    if (c == 0x08 || c == 0x7F) {
        if (s_line_len > 0U) {
            s_line_len--;
            s_line[s_line_len] = '\0';
            shell_out("\b \b", 3U);
        }
        return;
    }
    if (c == '\t') {
        shell_complete();
        return;
    }
    if (c == 0x1B) {                    /* ESC 序列（历史） */
        s_last_esc = true;
        return;
    }
    if (s_last_esc && c == '[') {
        return;
    }
    if (s_last_esc && (c == 'A' || c == 'B')) {
        s_last_esc = false;
        if (s_hist_count == 0U) {
            return;
        }
        if (c == 'A' && s_hist_pos > 0U) {
            s_hist_pos--;
        } else if (c == 'B' && s_hist_pos + 1U < s_hist_count) {
            s_hist_pos++;
        }
        shell_line_clear();
        if (s_hist_pos < s_hist_count) {
            const char *h = s_hist[s_hist_pos % SHELL_HISTORY_MAX];

            (void)snprintf(s_line, sizeof(s_line), "%s", h);
            s_line_len = (uint16_t)strlen(s_line);
            shell_out(s_line, s_line_len);
        }
        return;
    }
    s_last_esc = false;

    if (c >= 0x20 && c < 0x7F && s_line_len + 1U < SHELL_LINE_MAX) {
        s_line[s_line_len++] = (char)c;
        s_line[s_line_len] = '\0';
        shell_putchar((char)c);
    }
}

/* ------------------------------ Shell 任务 ------------------------------ */

static void shell_task_entry(void *arg)
{
    (void)arg;

    shell_prompt();
    for (;;) {
        int c = shell_rx_pop();

        if (c < 0) {
            iot_sleep_ms(10U);
            continue;
        }
        shell_feed_char(c);
    }
}

int shell_start(void)
{
    static iot_task_t *s_task;

    if (s_task != NULL) {
        return 0;
    }
    s_task = iot_task_create_dynamic("shell", (uint8_t)SHELL_SHELL_PRIO,
                                     shell_task_entry, NULL,
                                     (uint32_t)SHELL_SHELL_STACK);
    return (s_task != NULL) ? 0 : -1;
}
