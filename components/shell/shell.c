/*
 * shell.c - Shell 实例管理、行编辑与输入分派
 *
 * 每个 shell_t 实例拥有独立的变量表 / JOB 表 / 输入缓冲 / 行编辑 /
 * 粘贴缓冲；默认实例由 shell_setup() 创建，虚拟实例由
 * shell_instance_create() 创建。
 *
 * 输出：统一使用 ros_io.h 的 stdio 接口（ros_printf / ros_puts /
 *       ros_putchar / ros_io_write），由当前任务 stdio 决定去向。
 * 输入：shell_instance_feed() 投递字节到实例环形缓冲；输入任务取出后
 *       由行编辑器处理（回显、退格、Ctrl-C/Ctrl-D、Tab 补全）。
 * 分派：SHELL_CTX 命令优先 → 脚本语句；块（IF/FOR/WHILE…）累积到粘贴
 *       缓冲，闭合后作为匿名 JOB 同步执行。行首 "-" 表示不阻塞执行
 *       （如 -stepper 1 move 100），命令交给独立 JOB 任务后立即回到提示符。
 */

#include "shell_internal.h"
#include "shell_dev.h"

#include <string.h>
#include <stdio.h>

/* ------------------------------ 全局状态 ------------------------------ */

static shell_t *s_default;              /* 默认实例（平台绑定） */

/* 惰性实例化：只保存配置，首次 shell_lazy_start() 才创建输入任务 */
static shell_config_t s_lazy_cfg;
static bool s_lazy_ready;
static ros_task_t *s_lazy_task;         /* 按需实例的输入任务（实例由它自建） */

/* ------------------------------ 错误输出 ------------------------------ */

void shell_report_error(shell_t *sh, const shell_err_t *err, const char *job)
{
    (void)sh;
    if (err == NULL) {
        return;
    }
    if (err->code == SHELL_ERR_PARSE) {
        if (job != NULL) {
            ros_printf("PARSE L%u: %s @JOB %s\n", (unsigned)err->line,
                       err->msg, job);
        } else {
            ros_printf("PARSE L%u: %s\n", (unsigned)err->line, err->msg);
        }
        return;
    }
    if (job != NULL) {
        ros_printf("ERR %d: %s @JOB %s L%u\n", err->code, err->msg, job,
                   (unsigned)err->line);
    } else {
        ros_printf("ERR %d: %s\n", err->code, err->msg);
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
    memset(sh->paste, 0, SHELL_PASTE_MAX);

    sh->rx_head = 0U;
    sh->rx_tail = 0U;
    sh->ctrlc = false;
    sh->line_len = 0U;
    sh->last_esc = false;
    sh->last_cr = false;
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

    sh = (shell_t *)ros_mem_calloc(1U, sizeof(*sh));
    if (sh == NULL) {
        return NULL;
    }
    sh->cfg = *cfg;

    /* JOB 链表头为空，shell_job_t 在加载时按需分配 */
    sh->jobs = NULL;
    sh->rx = (char *)ros_mem_malloc(SHELL_RX_MAX);
    sh->line = (char *)ros_mem_malloc(SHELL_LINE_MAX);
    sh->paste = (char *)ros_mem_malloc(SHELL_PASTE_MAX);

    if (sh->rx == NULL || sh->line == NULL || sh->paste == NULL) {
        ros_mem_free(sh->rx);
        ros_mem_free(sh->line);
        ros_mem_free(sh->paste);
        ros_mem_free(sh);
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
    ros_mem_free(sh->rx);
    ros_mem_free(sh->line);
    ros_mem_free(sh->paste);
    if (s_default == sh) {
        s_default = NULL;
    }
    ros_mem_free(sh);
}

int shell_setup(const shell_config_t *cfg)
{
    if (cfg == NULL) {
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

/* 回收已结束的后台 JOB（任务名以 '@' 开头），释放其内存 */
static void shell_async_reap(shell_t *sh)
{
    shell_job_t *job = shell_job_first(sh);
    shell_job_t *next;

    while (job != NULL) {
        next = shell_job_next(job);
        if (job->name[0] == '@' &&
            (job->state == JOB_COMPLETED || job->state == JOB_ERROR ||
             job->state == JOB_ABORTED) &&
            (job->task == NULL || job->task->state == ROS_TASK_TERMINATED)) {
            shell_job_unload(job);
        }
        job = next;
    }
}

/*
 * 后台执行："-" 前缀的命令（如 -stepper 1 move 100）。
 * 包装成 JOB 脚本 "SHELL <命令>"，由独立任务执行，提交后立即返回提示符；
 * 任务名 @1、@2…，可用 jobs / stop / waitjob / fg 管理。
 */
static int shell_instance_exec_async(shell_t *sh, const char *cmd,
                                     const char *args)
{
    char text[SHELL_LINE_MAX + 8U];
    char name[SHELL_VAR_NAME_MAX];
    shell_job_t *job;
    shell_err_t err;
    unsigned seq;
    int n;

    if (*cmd == '\0') {
        shell_error(sh, SHELL_ERR_PARAM, "usage: -<command> [args]");
        return -SHELL_ERR_PARAM;
    }

    n = snprintf(text, sizeof(text), "SHELL %s%s%s", cmd,
                 (*args != '\0') ? " " : "", args);
    if (n <= 0 || n >= (int)sizeof(text)) {
        shell_error(sh, SHELL_ERR_PARAM, "command too long");
        return -SHELL_ERR_PARAM;
    }

    shell_async_reap(sh);

    seq = (unsigned)sh->job_seq + 1U;
    do {
        (void)snprintf(name, sizeof(name), "@%u", seq++);
    } while (shell_job_find(sh, name, (uint16_t)strlen(name)) != NULL);

    job = shell_job_load(sh, text, (uint32_t)n, name, 0U, 0U, &err);
    if (job == NULL) {
        shell_report_error(sh, &err, NULL);
        return -(err.code != 0 ? err.code : SHELL_ERR_RESOURCE);
    }
    if (shell_job_start(job) != 0) {
        shell_error(sh, SHELL_ERR_STATE, "job %s start failed", name);
        shell_job_unload(job);
        return -SHELL_ERR_STATE;
    }
    if (*args != '\0') {
        ros_printf("[%s] %s %s\n", job->name, cmd, args);
    } else {
        ros_printf("[%s] %s\n", job->name, cmd);
    }
    return SHELL_OK;
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

    /* "-" 前缀：后台执行，立即返回提示符 */
    if (tok[0] == '-') {
        return shell_instance_exec_async(sh, tok + 1, rest);
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

int shell_exec_isolated(shell_t *parent, const char *text)
{
    shell_config_t cfg;
    shell_t *child;
    int rc;

    if (parent == NULL || text == NULL) {
        return -SHELL_ERR_PARAM;
    }

    /* 输出直接走当前任务 stdio，与父实例一致，无需额外转发后端 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = parent->cfg.version;
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
        ros_puts("(empty)");
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
        ros_io_write(NULL, "...> ", 5U);
    } else {
        ros_io_write(NULL, "> ", 2U);
    }
}

/* 提交一行：块累积或直接执行 */
static void shell_submit_line(shell_t *sh)
{
    char tok_buf[SHELL_LINE_MAX];
    char *tok;
    char *rest;
    uint16_t len = sh->line_len;

    sh->line[sh->line_len] = '\0';
    ros_io_write(NULL, "\n", 1U);

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
    if (!sh->exit_req) {
        /* exit 已请求关闭：不再打印提示符，任务随后退出 */
        shell_prompt(sh);
    }
}

/* Tab 补全：只补命令名，多个候选时列出 */
static void shell_complete(shell_t *sh)
{
    static const char *const names[] = {
        "help", "ver", "echo", "print", "set", "unset", "wait", "reboot",
        "state", "ps", "kill", "pause", "resume", "load", "unload",
        "loads", "run", "start", "stop", "fg", "jobs", "job", "waitjob",
        "gpio", "pwm", "stepper", "fan"
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
            ros_putchar(sh->line[sh->line_len - 1U]);
        }
        sh->line[sh->line_len] = '\0';
    } else if (matches > 1U) {
        ros_io_write(NULL, "\n", 1U);
        for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
            if (strncmp(names[i], prefix, n) == 0) {
                ros_io_write(NULL, names[i], (uint32_t)strlen(names[i]));
                ros_io_write(NULL, " ", 1U);
            }
        }
        ros_io_write(NULL, "\n", 1U);
        shell_prompt(sh);
        ros_io_write(NULL, sh->line, sh->line_len);
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
        ros_puts("^C");
        shell_prompt(sh);
        return;
    }
    if (c == 0x04) {                    /* Ctrl-D：执行粘贴缓冲 */
        (void)shell_run_paste_buffer(sh);
        shell_prompt(sh);
        return;
    }
    if (c == '\r' || c == '\n') {
        /* 串口终端可能只发 CR，也可能发 CRLF：都按一次提交处理 */
        if (c == '\n' && sh->last_cr) {
            sh->last_cr = false;
            return;
        }
        sh->last_cr = (c == '\r');
        shell_submit_line(sh);
        return;
    }
    sh->last_cr = false;
    if (c == 0x08 || c == 0x7F) {
        if (sh->line_len > 0U) {
            sh->line_len--;
            sh->line[sh->line_len] = '\0';
            ros_io_write(NULL, "\b \b", 3U);
        }
        return;
    }
    if (c == '\t') {
        shell_complete(sh);
        return;
    }
    if (c == 0x1B) {                    /* ESC：方向键等控制序列，整段丢弃 */
        sh->last_esc = true;
        return;
    }
    if (sh->last_esc) {
        /* CSI 序列 ESC [ 参数... 终止符：在终止字节前保持吞掉状态 */
        if (c == '[' || (c >= '0' && c <= '9') || c == ';') {
            return;
        }
        sh->last_esc = false;
        return;
    }

    if (c >= 0x20 && c < 0x7F && sh->line_len + 1U < SHELL_LINE_MAX) {
        sh->line[sh->line_len++] = (char)c;
        sh->line[sh->line_len] = '\0';
        ros_putchar((char)c);
    }
}

/* ------------------------------ Shell 任务 ------------------------------ */

/* 按需实例内置 exit：请求任务退出，平台随后回收实例内存 */
static int shell_cmd_exit_builtin(shell_t *sh, int argc, char **argv)
{
    (void)sh;
    (void)argc;
    (void)argv;

    ros_printf("bye\n");
    shell_request_exit();
    return SHELL_OK;
}

static const shell_cmd_t s_exit_cmd = {
    "exit", shell_cmd_exit_builtin, "exit", "close shell, back to listening",
    "basic", CMD_NONE
};

/*
 * 从 stdio 读取输入并交给行编辑器；返回 true 表示本轮有进展。
 * read < 0 视为输入结束（管道 EOF / 连接断开）：请求关闭实例，
 * 由平台监听任务回收。
 */
static bool shell_input_poll(shell_t *sh)
{
    ros_io_t *io = ros_stdio();
    char buf[32];
    int n;
    int i;

    if (io->read == NULL) {
        return false;
    }
    n = io->read(io, buf, sizeof(buf));
    if (n < 0) {
        sh->exit_req = true;
        return true;
    }
    for (i = 0; i < n; i++) {
        shell_feed_char(sh, (unsigned char)buf[i]);
        if (sh->exit_req) {
            break;
        }
    }
    return n > 0;
}

static void shell_task_entry(void *arg)
{
    shell_t *sh = (shell_t *)arg;

    if (sh == NULL) {
        /* 按需实例：实例缓冲由任务自己申请，内存记账落在本任务名下 */
        if (!s_lazy_ready) {
            ros_task_exit();
            return;
        }
        sh = shell_instance_create(&s_lazy_cfg);
        if (sh == NULL) {
            ros_task_exit();
            return;
        }
        s_default = sh;
        (void)shell_register_cmd(&s_exit_cmd);
        /* 设备命令（gpio / pwm / stepper）随实例一起注册 */
        (void)shell_dev_register();
    }
    sh->task = ros_current_task;

    shell_prompt(sh);
    for (;;) {
        int c = shell_rx_pop(sh);

        if (c >= 0) {
            shell_feed_char(sh, c);
            if (sh->exit_req) {
                /* 平台已请求关闭（exit 命令）：任务自行退出，由监听任务回收 */
                ros_task_exit();
            }
            continue;
        }
        if (shell_input_poll(sh)) {
            if (sh->exit_req) {
                ros_task_exit();    /* exit 命令或输入已结束 */
            }
            continue;
        }
        ros_sleep_ms(10U);
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
    sh->task = ros_task_create_dynamic("shell", (uint8_t)SHELL_SHELL_PRIO,
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

/* ---------------------- 按需实例化 / 退出回收 ---------------------- */

int shell_lazy_setup(const shell_config_t *cfg)
{
    if (cfg == NULL) {
        return -1;
    }
    s_lazy_cfg = *cfg;
    s_lazy_ready = true;
    return 0;
}

int shell_lazy_start(void)
{
    if (s_lazy_task != NULL) {
        return 0;
    }
    if (!s_lazy_ready) {
        return -1;
    }

    /*
     * 只创建输入任务；实例缓冲（约 1.4KB）由任务入口自己申请，
     * 这样 ps 的内存归属统计会落在 shell 任务名下，而不是创建者。
     */
    s_lazy_task = ros_task_create_dynamic("shell", (uint8_t)SHELL_SHELL_PRIO,
                                          shell_task_entry, NULL,
                                          (uint32_t)SHELL_SHELL_STACK);
    return (s_lazy_task != NULL) ? 0 : -1;
}

bool shell_started(void)
{
    return (s_lazy_task != NULL || s_default != NULL);
}

bool shell_active(void)
{
    return s_default != NULL;
}

void shell_request_exit(void)
{
    if (s_default != NULL) {
        s_default->exit_req = true;
    }
}

bool shell_task_finished(void)
{
    if (s_default != NULL && s_default->task != NULL) {
        return (s_default->task->state == ROS_TASK_TERMINATED);
    }
    return (s_lazy_task != NULL &&
            s_lazy_task->state == ROS_TASK_TERMINATED);
}

int shell_shutdown(void)
{
    ros_task_t *task = NULL;

    if (s_default != NULL) {
        task = s_default->task;
        if (task != NULL && task->state != ROS_TASK_TERMINATED) {
            return -1;              /* 任务仍在运行，不能释放其栈 */
        }
        s_default->task = NULL;
        shell_instance_destroy(s_default);   /* 内部会把 s_default 置 NULL */
    }
    if (task != NULL) {
        (void)ros_task_destroy(task);
    }
    if (s_lazy_task != NULL) {
        if (s_lazy_task != task) {
            if (s_lazy_task->state != ROS_TASK_TERMINATED) {
                return -1;
            }
            (void)ros_task_destroy(s_lazy_task);
        }
        s_lazy_task = NULL;
    }
    return 0;
}
