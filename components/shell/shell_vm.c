/*
 * shell_vm.c - 脚本虚拟机与 JOB 生命周期
 *
 * 执行模型：
 *   - 每个 JOB 一个独立 RTOS 任务（shell_job_start）；
 *   - shell_job_run_sync() 在当前上下文同步执行，供测试与调试，
 *     不创建任务、不阻塞 WAIT（等待时间被跳过）。
 * 指令是解析期编译好的线性数组，运行时只有 pc 与循环帧栈。
 */

#include "shell_internal.h"

#include <string.h>
#include <stdio.h>

#define SHELL_TAG "shell"

shell_vars_t g_shell_vars;

static shell_job_t s_jobs[SHELL_JOB_MAX];
static uint16_t    s_job_seq;
static iot_mutex_t s_out_mutex;      /* 输出原子性 */

/* ------------------------------ 输出互斥 ------------------------------ */

int shell_out_init(void)
{
    return iot_mutex_init(&s_out_mutex);
}

void shell_out_lock(void)
{
    /* 调度器未启动（初始化 / 同步测试）时不存在并发，跳过互斥 */
    if (iot_current_task != NULL) {
        (void)iot_mutex_lock(&s_out_mutex, IOT_TIMEOUT_FOREVER);
    }
}

void shell_out_unlock(void)
{
    if (iot_current_task != NULL) {
        (void)iot_mutex_unlock(&s_out_mutex);
    }
}

/* ------------------------------ 循环帧 ------------------------------ */

typedef struct {
    bool used;
    char var[SHELL_VAR_NAME_MAX];
    int32_t begin;
    int32_t end;
    int32_t step;
    uint16_t body;          /* 循环体首指令 */
    uint16_t next;          /* FOR_NEXT 指令下标 */
} loop_frame_t;

/* ------------------------------ 变量辅助 ------------------------------ */

static void shell_var_update(shell_vars_t *vars, const char *name,
                             const shell_value_t *val)
{
    shell_var_t *var = shell_var_find(vars, name, (uint16_t)strlen(name));

    if (var == NULL) {
        shell_vars_set(vars, name, (uint16_t)strlen(name), val, true, NULL);
        return;
    }
    /* 只读内建变量由 VM 内部更新 */
    var->readonly = false;
    shell_vars_set(vars, name, (uint16_t)strlen(name), val, true, NULL);
}

static void shell_set_int_var(shell_vars_t *vars, const char *name, int32_t v)
{
    shell_value_t val;

    memset(&val, 0, sizeof(val));
    val.type = SHELL_VAL_INT;
    val.i = v;
    shell_var_update(vars, name, &val);
}

static void shell_set_str_var(shell_vars_t *vars, const char *name,
                              const char *s, uint16_t len)
{
    shell_value_t val;

    memset(&val, 0, sizeof(val));
    val.type = SHELL_VAL_STRING;
    val.s = s;
    val.slen = len;
    shell_var_update(vars, name, &val);
}

/* ------------------------------ 状态名 ------------------------------ */

const char *shell_job_state_name(uint8_t state)
{
    switch (state) {
    case JOB_LOADED:    return "LOADED";
    case JOB_RUNNING:   return "RUNNING";
    case JOB_PAUSED:    return "PAUSED";
    case JOB_STOPPING:  return "STOPPING";
    case JOB_COMPLETED: return "COMPLETED";
    case JOB_ERROR:     return "ERROR";
    case JOB_ABORTED:   return "ABORTED";
    default:            return "FREE";
    }
}

/* ------------------------------ JOB 表 ------------------------------ */

static bool shell_name_is_reserved(const char *name, uint16_t len)
{
    static const char *const reserved[] = {
        "shell", "idle", "timer", "uart_rx"
    };
    uint32_t i;

    for (i = 0U; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (shell_ieq(name, len, reserved[i])) {
            return true;
        }
    }
    return false;
}

/* 名字比较：a 为 C 字符串，b 为定长片段，大小写折叠 */
static bool shell_name_eq(const char *a, const char *b, uint16_t len)
{
    uint16_t i;

    if (a == NULL || b == NULL || strlen(a) != len) {
        return false;
    }
    for (i = 0U; i < len; i++) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 'a' + 'A');
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

shell_job_t *shell_job_find(const char *name, uint16_t len)
{
    uint16_t i;

    if (name == NULL || len == 0U) {
        return NULL;
    }
    for (i = 0U; i < SHELL_JOB_MAX; i++) {
        if (s_jobs[i].used && shell_name_eq(s_jobs[i].name, name, len)) {
            return &s_jobs[i];
        }
    }
    return NULL;
}

shell_job_t *shell_job_find_by_task(const iot_task_t *task)
{
    uint16_t i;

    for (i = 0U; i < SHELL_JOB_MAX; i++) {
        if (s_jobs[i].used && s_jobs[i].task == task) {
            return &s_jobs[i];
        }
    }
    return NULL;
}

/* 结束的任务若已 TERMINATED，顺手回收 TCB */
static void shell_job_reap(shell_job_t *job)
{
    if (job->task != NULL &&
        (job->state == JOB_COMPLETED || job->state == JOB_ERROR ||
         job->state == JOB_ABORTED)) {
        if (job->task->state == IOT_TASK_TERMINATED) {
            (void)iot_task_destroy(job->task);
            job->task = NULL;
        }
    }
}

static void shell_job_free(shell_job_t *job)
{
    if (!job->used) {
        return;
    }
    if (job->task != NULL) {
        if (job->task->state != IOT_TASK_TERMINATED &&
            job->task != iot_current_task) {
            job->stop_flag = true;
        }
        if (job->task->state == IOT_TASK_TERMINATED) {
            (void)iot_task_destroy(job->task);
        }
        job->task = NULL;
    }
    shell_script_free(&job->script);
    if (job->text != NULL) {
        iot_mem_free(job->text);
        job->text = NULL;
    }
    memset(job, 0, sizeof(*job));
}

void shell_jobs_clear(void)
{
    uint16_t i;

    for (i = 0U; i < SHELL_JOB_MAX; i++) {
        shell_job_free(&s_jobs[i]);
    }
    s_job_seq = 0U;
    shell_vars_clear(&g_shell_vars);
}

/* 按槽位取 JOB（空槽返回 NULL），供 jobs/loads 遍历 */
shell_job_t *shell_job_slot(uint16_t index)
{
    if (index >= SHELL_JOB_MAX || !s_jobs[index].used) {
        return NULL;
    }
    return &s_jobs[index];
}

/* 卸载单个 JOB（调用方已校验状态） */
void shell_job_unload(shell_job_t *job)
{
    shell_job_free(job);
}

shell_job_t *shell_job_load(const char *text, uint32_t len,
                            const char *name, uint8_t prio, uint32_t stack,
                            shell_err_t *err)
{
    shell_job_t *job = NULL;
    shell_err_t local;
    uint16_t i;
    uint16_t nlen;

    if (err == NULL) {
        err = &local;
    }
    memset(err, 0, sizeof(*err));

    if (text == NULL || len == 0U || name == NULL) {
        err->code = SHELL_ERR_PARAM;
        snprintf(err->msg, sizeof(err->msg), "invalid script");
        return NULL;
    }

    nlen = (uint16_t)strlen(name);
    if (nlen == 0U || nlen >= SHELL_VAR_NAME_MAX) {
        err->code = SHELL_ERR_NAME;
        snprintf(err->msg, sizeof(err->msg), "invalid job name");
        return NULL;
    }
    if (shell_name_is_reserved(name, nlen)) {
        err->code = SHELL_ERR_NAME;
        snprintf(err->msg, sizeof(err->msg), "reserved name");
        return NULL;
    }
    if (shell_job_find(name, nlen) != NULL) {
        err->code = SHELL_ERR_NAME;
        snprintf(err->msg, sizeof(err->msg), "job %.*s already exists",
                 (int)nlen, name);
        return NULL;
    }
    for (i = 0U; i < SHELL_JOB_MAX; i++) {
        if (!s_jobs[i].used) {
            job = &s_jobs[i];
            break;
        }
    }
    if (job == NULL) {
        err->code = SHELL_ERR_RESOURCE;
        snprintf(err->msg, sizeof(err->msg), "too many jobs");
        return NULL;
    }

    memset(job, 0, sizeof(*job));
    job->text = (char *)iot_mem_malloc(len + 1U);
    if (job->text == NULL) {
        err->code = SHELL_ERR_RESOURCE;
        snprintf(err->msg, sizeof(err->msg), "out of memory");
        return NULL;
    }
    memcpy(job->text, text, len);
    job->text[len] = '\0';
    job->text_len = len;

    if (shell_script_parse(&job->script, job->text, err) != 0) {
        iot_mem_free(job->text);
        memset(job, 0, sizeof(*job));
        return NULL;
    }

    job->used = true;
    job->job_id = ++s_job_seq;
    for (i = 0U; i < nlen; i++) {
        job->name[i] = (name[i] >= 'a' && name[i] <= 'z')
                       ? (char)(name[i] - 'a' + 'A') : name[i];
    }
    job->name[nlen] = '\0';
    job->prio = (prio != 0U) ? prio : (uint8_t)SHELL_JOB_PRIO;
    job->stack = (stack != 0U) ? stack : (uint32_t)SHELL_JOB_STACK;
    job->state = JOB_LOADED;
    shell_vars_init(&job->vars);
    return job;
}

/* ------------------------------ 表达式 / 参数解析 ------------------------------ */

/* 对脚本中的一段文本求值 */
static int shell_eval_text(shell_vars_t *vars, const char *text, uint16_t len,
                           shell_value_t *out, shell_err_t *err, uint16_t line)
{
    shell_tok_t toks[SHELL_TOKEN_MAX];
    int n = shell_lex(text, len, toks, SHELL_TOKEN_MAX);

    if (n <= 1) {
        err->code = SHELL_ERR_PARSE;
        err->line = line;
        snprintf(err->msg, sizeof(err->msg), "missing expression");
        return -1;
    }
    if (shell_expr_eval(toks, (uint16_t)n, vars, out, err) != 0) {
        err->line = line;
        return -1;
    }
    return 0;
}

/* SET $VAR = expr：拆出变量名与右值 */
static int shell_exec_set(shell_job_t *job, const shell_instr_t *ins,
                          shell_err_t *err)
{
    const char *arg = &job->script.text[ins->arg_off];
    shell_tok_t toks[SHELL_TOKEN_MAX];
    int n = shell_lex(arg, ins->arg_len, toks, SHELL_TOKEN_MAX);
    int eq = -1;
    int i;
    shell_value_t val;
    shell_err_t local;

    if (n < 4 || toks[0].type != SHELL_TOK_VAR) {
        err->code = SHELL_ERR_PARSE;
        err->line = ins->line;
        snprintf(err->msg, sizeof(err->msg), "SET expects $VAR = expr");
        return -1;
    }
    for (i = 1; i < n - 1; i++) {
        if (toks[i].type == SHELL_TOK_OP && toks[i].len == 1U &&
            toks[i].p[0] == '=') {
            eq = i;
            break;
        }
    }
    if (eq < 0) {
        err->code = SHELL_ERR_PARSE;
        err->line = ins->line;
        snprintf(err->msg, sizeof(err->msg), "SET expects $VAR = expr");
        return -1;
    }

    memset(&local, 0, sizeof(local));
    if (shell_expr_eval(&toks[eq + 1], (uint16_t)(n - eq - 1),
                        &job->vars, &val, &local) != 0) {
        *err = local;
        err->line = ins->line;
        return -1;
    }
    if (shell_vars_set(&job->vars, toks[0].p + 1,
                       (uint16_t)(toks[0].len - 1U), &val, false,
                       &local) != 0) {
        *err = local;
        err->line = ins->line;
        return -1;
    }
    return 0;
}

/* 取参数首 token 作为名字（START JOB FEED） */
static void shell_arg_name(const shell_job_t *job, const shell_instr_t *ins,
                           const char **name, uint16_t *len)
{
    shell_tok_t toks[4];
    int n = shell_lex(&job->script.text[ins->arg_off], ins->arg_len,
                      toks, 4);

    if (n >= 1 && toks[0].type != SHELL_TOK_EOF) {
        *name = toks[0].p;
        *len = toks[0].len;
    } else {
        *name = "";
        *len = 0U;
    }
}

/* ------------------------------ 钩子与清理 ------------------------------ */

static int shell_vm_exec(shell_job_t *job, uint16_t limit, bool verbose);

static void shell_vm_cleanup(shell_job_t *job)
{
    job->pc = 0U;
    shell_set_int_var(&job->vars, "PC", 0);
}

/* 在 [start, end) 区间执行钩子；in_handler 防止递归触发 */
static void shell_vm_run_hook(shell_job_t *job, uint16_t start, uint16_t end,
                              bool verbose)
{
    uint16_t saved_pc = job->pc;
    bool saved_flag = job->stop_flag;
    bool saved_handler = job->in_handler;

    if (start == 0U || end == 0U || start >= end ||
        start >= job->script.count) {
        return;
    }
    job->pc = start;
    job->stop_flag = false;
    job->in_handler = true;

    (void)shell_vm_exec(job, (end > job->script.count) ? job->script.count : end,
                        verbose);

    job->in_handler = saved_handler;
    job->pc = saved_pc;
    job->stop_flag = saved_flag;
}

/* ------------------------------ VM 主循环 ------------------------------ */

/* 对 token 的连续子区间求值（内部补 EOF） */
static int shell_eval_span(shell_vars_t *vars, const shell_tok_t *toks,
                           uint16_t from, uint16_t count,
                           shell_value_t *out, shell_err_t *err)
{
    shell_tok_t tmp[SHELL_TOKEN_MAX];
    uint16_t n = (count < SHELL_TOKEN_MAX - 1U) ? count
                                                : (uint16_t)(SHELL_TOKEN_MAX - 1U);

    memcpy(tmp, &toks[from], (size_t)n * sizeof(tmp[0]));
    memset(&tmp[n], 0, sizeof(tmp[n]));
    tmp[n].type = SHELL_TOK_EOF;
    return shell_expr_eval(tmp, (uint16_t)(n + 1U), vars, out, err);
}

/* 执行 FOR_INIT / FOR_NEXT 的参数解析，结果写入变量表 */
static int shell_loop_init(shell_job_t *job, const shell_instr_t *ins,
                           loop_frame_t *frame, shell_err_t *err)
{
    const char *arg = &job->script.text[ins->arg_off];
    shell_tok_t toks[SHELL_TOKEN_MAX];
    int n = shell_lex(arg, ins->arg_len, toks, SHELL_TOKEN_MAX);
    int to = -1;
    int step = -1;
    int i;
    int32_t begin = 1;
    int32_t end = 0x7FFFFFFF;
    int32_t inc = 1;
    shell_value_t v;

    memset(frame, 0, sizeof(*frame));

    /* LOOP [N]：没有 $VAR = 结构 */
    if (n <= 1 || toks[0].type != SHELL_TOK_VAR) {
        (void)snprintf(frame->var, sizeof(frame->var), "%s", "__I");
        if (n >= 2) {
            if (shell_eval_span(&job->vars, toks, 0U, (uint16_t)(n - 1U),
                                &v, err) != 0) {
                err->line = ins->line;
                return -1;
            }
            (void)shell_value_to_int(&v, &end, err);
        }
        frame->used = true;
        frame->begin = begin;
        frame->end = end;
        frame->step = 1;
        return 0;
    }

    for (i = 1; i < n - 1; i++) {
        if (toks[i].type == SHELL_TOK_WORD && shell_tok_ieq(&toks[i], "TO")) {
            to = i;
        } else if (toks[i].type == SHELL_TOK_WORD &&
                   shell_tok_ieq(&toks[i], "STEP")) {
            step = i;
        }
    }
    if (to < 0) {
        err->code = SHELL_ERR_PARSE;
        err->line = ins->line;
        snprintf(err->msg, sizeof(err->msg), "FOR expects TO");
        return -1;
    }

    /* 起始值 tokens[2..to) */
    if (shell_eval_span(&job->vars, toks, 2U, (uint16_t)(to - 2),
                        &v, err) != 0) {
        err->line = ins->line;
        return -1;
    }
    (void)shell_value_to_int(&v, &begin, err);

    if (step >= 0) {
        if (shell_eval_span(&job->vars, toks, (uint16_t)(to + 1),
                            (uint16_t)(step - to - 1), &v, err) != 0) {
            err->line = ins->line;
            return -1;
        }
        (void)shell_value_to_int(&v, &end, err);
        if (shell_eval_span(&job->vars, toks, (uint16_t)(step + 1),
                            (uint16_t)(n - step - 1), &v, err) != 0) {
            err->line = ins->line;
            return -1;
        }
        (void)shell_value_to_int(&v, &inc, err);
    } else {
        if (shell_eval_span(&job->vars, toks, (uint16_t)(to + 1),
                            (uint16_t)(n - to - 1), &v, err) != 0) {
            err->line = ins->line;
            return -1;
        }
        (void)shell_value_to_int(&v, &end, err);
    }

    if (inc == 0) {
        err->code = SHELL_ERR_PARAM;
        err->line = ins->line;
        snprintf(err->msg, sizeof(err->msg), "FOR STEP 0");
        return -1;
    }

    if (toks[0].len <= 1U) {
        err->code = SHELL_ERR_PARSE;
        err->line = ins->line;
        snprintf(err->msg, sizeof(err->msg), "invalid loop variable");
        return -1;
    }
    memcpy(frame->var, toks[0].p + 1,
           toks[0].len - 1U < SHELL_VAR_NAME_MAX - 1U
           ? (size_t)(toks[0].len - 1U) : SHELL_VAR_NAME_MAX - 1U);
    frame->var[toks[0].len - 1U < SHELL_VAR_NAME_MAX - 1U
               ? (toks[0].len - 1U) : SHELL_VAR_NAME_MAX - 1U] = '\0';
    frame->used = true;
    frame->begin = begin;
    frame->end = end;
    frame->step = inc;
    return 0;
}

/* 循环体是否继续：$VAR 加上步长后判断是否仍在区间内 */
static bool shell_loop_next(shell_job_t *job, loop_frame_t *frame,
                            shell_err_t *err, uint16_t line)
{
    shell_value_t v;
    int32_t cur = 0;

    if (shell_vars_get(&job->vars, frame->var,
                       (uint16_t)strlen(frame->var), &v, err) != 0) {
        err->line = line;
        return false;
    }
    (void)shell_value_to_int(&v, &cur, err);

    cur += frame->step;
    shell_set_int_var(&job->vars, frame->var, cur);

    if (frame->step > 0) {
        return cur <= frame->end;
    }
    return cur >= frame->end;
}

/* 可中断的毫秒等待：stop_flag 置位后立即返回 false */
static bool shell_vm_wait_ms(shell_job_t *job, uint32_t ms)
{
    if (job->task == NULL) {
        return !job->stop_flag;         /* 同步执行：不真正阻塞 */
    }
    while (ms > 0U) {
        uint32_t slice = (ms > 20U) ? 20U : ms;

        if (job->stop_flag) {
            return false;
        }
        (void)iot_sem_take(&job->wake, slice);
        if (job->stop_flag) {
            return false;
        }
        ms -= slice;
    }
    return true;
}

/* 执行 [current_pc, limit)；返回 0 正常、-1 出错、1 被 stop */
static int shell_vm_exec(shell_job_t *job, uint16_t limit, bool verbose)
{
    loop_frame_t loops[SHELL_BLOCK_DEPTH];
    uint8_t loop_depth = 0U;
    shell_err_t err;

    memset(loops, 0, sizeof(loops));
    memset(&err, 0, sizeof(err));

    while (job->pc < limit && job->pc < job->script.count) {
        const shell_instr_t *ins = &job->script.code[job->pc];
        shell_value_t val;
        int rc = 0;

        if (job->stop_flag) {
            return 1;
        }
        job->pc++;

        switch (ins->op) {
        case SHELL_OP_NOP:
        case SHELL_OP_ENDJOB:
            break;

        case SHELL_OP_SET:
            rc = shell_exec_set(job, ins, &err);
            break;

        case SHELL_OP_PRINT:
        case SHELL_OP_SHELL_PRINT:
        case SHELL_OP_SHELL_LOG: {
            char buf[96];
            uint16_t out_len = 0U;

            if (shell_eval_text(&job->vars, &job->script.text[ins->arg_off],
                                ins->arg_len, &val, &err, ins->line) != 0) {
                rc = -1;
                break;
            }
            shell_value_format(&val, buf, sizeof(buf), &out_len);
            if (verbose) {
                shell_printf_job(job->name, "%.*s\n", (int)out_len, buf);
            }
            break;
        }

        case SHELL_OP_SHELL_STATE: {
            const char *arg = &job->script.text[ins->arg_off];
            char state[96];
            int n;

            n = snprintf(state, sizeof(state),
                         "tasks=%u;jobs=%u;job=%s;state=%s",
                         (unsigned)iot_task_total(), 1U, job->name,
                         shell_job_state_name(job->state));
            if (n > 0 && ins->arg_len > 0U) {
                shell_tok_t toks[4];
                int tn = shell_lex(arg, ins->arg_len, toks, 4);

                if (tn >= 2 && shell_tok_ieq(&toks[0], "TO") &&
                    toks[1].type == SHELL_TOK_VAR && toks[1].len > 1U) {
                    char name[SHELL_VAR_NAME_MAX];
                    uint16_t nlen = (uint16_t)(toks[1].len - 1U);

                    if (nlen >= SHELL_VAR_NAME_MAX) {
                        nlen = (uint16_t)(SHELL_VAR_NAME_MAX - 1U);
                    }
                    memcpy(name, toks[1].p + 1, nlen);
                    name[nlen] = '\0';
                    shell_set_str_var(&job->vars, name, state, (uint16_t)n);
                }
            }
            break;
        }

        case SHELL_OP_SHELL_CMD: {
            char text[96];
            char *argv[6];
            int argc = 0;
            uint16_t len = ins->arg_len;
            uint16_t i;
            uint16_t start = 0U;

            if (len >= sizeof(text)) {
                len = (uint16_t)(sizeof(text) - 1U);
            }
            memcpy(text, &job->script.text[ins->arg_off], len);
            text[len] = '\0';
            for (i = 0U; i <= len && argc < 6; i++) {
                if (i == len || text[i] == ' ') {
                    if (i > start) {
                        text[i] = '\0';
                        argv[argc++] = &text[start];
                    }
                    start = (uint16_t)(i + 1U);
                }
            }
            if (argc > 0) {
                rc = shell_cmd_dispatch(argc, argv);
            }
            break;
        }

        case SHELL_OP_IF_FALSE:
        case SHELL_OP_WHILE_CHECK: {
            uint16_t len = ins->arg_len;

            if (shell_eval_text(&job->vars, &job->script.text[ins->arg_off],
                                len, &val, &err, ins->line) != 0) {
                rc = -1;
                break;
            }
            {
                bool truth = (val.type == SHELL_VAL_BOOL) ? val.b
                            : (val.type == SHELL_VAL_FLOAT) ? (val.f != 0.0f)
                            : (val.type == SHELL_VAL_INT) ? (val.i != 0)
                            : false;

                if (val.type == SHELL_VAL_STRING) {
                    err.code = SHELL_ERR_TYPE;
                    err.line = ins->line;
                    snprintf(err.msg, sizeof(err.msg), "type mismatch");
                    rc = -1;
                } else if (!truth) {
                    job->pc = ins->target;
                }
            }
            break;
        }

        case SHELL_OP_JUMP:
        case SHELL_OP_GOTO:
            job->pc = ins->target;
            break;

        case SHELL_OP_FOR_INIT: {
            loop_frame_t *frame;

            if (loop_depth >= SHELL_BLOCK_DEPTH ||
                shell_loop_init(job, ins, &loops[loop_depth], &err) != 0) {
                rc = -1;
                break;
            }
            frame = &loops[loop_depth];

            /* 首次条件不满足：不改变循环变量，直接跳到 FOR_NEXT 之后 */
            if ((frame->step > 0 && frame->begin > frame->end) ||
                (frame->step < 0 && frame->begin < frame->end)) {
                job->pc = (uint16_t)(ins->target + 1U);
            } else {
                shell_set_int_var(&job->vars, frame->var, frame->begin);
                loop_depth++;
            }
            break;
        }

        case SHELL_OP_FOR_NEXT: {
            if (loop_depth == 0U) {
                rc = -1;
                break;
            }
            if (shell_loop_next(job, &loops[loop_depth - 1U], &err,
                                ins->line)) {
                job->pc = ins->target;
            } else {
                loop_depth--;
            }
            break;
        }

        case SHELL_OP_BREAK:
            job->pc = ins->target;
            if (loop_depth > 0U) {
                loop_depth--;
            }
            break;

        case SHELL_OP_CONTINUE:
            job->pc = ins->target;
            break;

        case SHELL_OP_WAIT_TIME: {
            uint32_t ms = 0U;

            if (shell_eval_text(&job->vars, &job->script.text[ins->arg_off],
                                ins->arg_len, &val, &err, ins->line) != 0) {
                rc = -1;
                break;
            }
            {
                int32_t iv = 0;

                (void)shell_value_to_int(&val, &iv, &err);
                ms = (uint32_t)((iv > 0) ? iv : 0);
            }
            if (!shell_vm_wait_ms(job, ms)) {
                return 1;
            }
            break;
        }

        case SHELL_OP_START_JOB:
        case SHELL_OP_STOP_JOB: {
            const char *name;
            uint16_t len;
            shell_job_t *target;

            shell_arg_name(job, ins, &name, &len);
            target = shell_job_find(name, len);
            if (target == NULL) {
                err.code = SHELL_ERR_NOT_FOUND;
                err.line = ins->line;
                snprintf(err.msg, sizeof(err.msg), "job not found");
                rc = -1;
                break;
            }
            if (ins->op == SHELL_OP_START_JOB) {
                if (shell_job_start(target) != 0) {
                    err.code = SHELL_ERR_STATE;
                    err.line = ins->line;
                    snprintf(err.msg, sizeof(err.msg), "job start failed");
                    rc = -1;
                }
            } else {
                (void)shell_job_stop(target, false);
            }
            break;
        }

        case SHELL_OP_CALL:
        case SHELL_OP_RETURN:
            /* 子程序：V1.1 暂只支持顺序 CALL（无嵌套返回栈） */
            if (ins->op == SHELL_OP_CALL) {
                job->pc = ins->target;
            }
            break;

        default:
            break;
        }

        if (rc != 0) {
            job->err_code = err.code;
            job->err_line = err.line ? err.line : ins->line;
            snprintf(job->err_msg, sizeof(job->err_msg), "%.*s",
                     (int)(sizeof(job->err_msg) - 1U),
                     err.msg[0] != '\0' ? err.msg : "runtime error");
            shell_set_int_var(&job->vars, "ERRCODE", job->err_code);
            shell_set_int_var(&job->vars, "ERRLINE", job->err_line);
            shell_set_str_var(&job->vars, "ERRMSG", job->err_msg,
                              (uint16_t)strlen(job->err_msg));
            return -1;
        }
    }
    return 0;
}

/* ------------------------------ 对外执行入口 ------------------------------ */

int shell_vm_run(shell_job_t *job, bool verbose)
{
    int rc;

    shell_set_str_var(&job->vars, "JOBNAME", job->name,
                      (uint16_t)strlen(job->name));
    shell_set_int_var(&job->vars, "TID",
                      job->task != NULL ? job->task->tid : 0);
    shell_set_int_var(&job->vars, "ERRCODE", 0);
    shell_set_int_var(&job->vars, "ERRLINE", 0);
    shell_set_int_var(&job->vars, "PC", 0);

    rc = shell_vm_exec(job, job->script.count, verbose);

    if (rc == 0) {
        job->state = JOB_COMPLETED;
        if (job->script.stop_start != 0U) {
            shell_vm_run_hook(job, job->script.stop_start,
                              job->script.stop_end, verbose);
        }
    } else if (rc > 0) {
        job->state = JOB_ABORTED;
        shell_set_int_var(&job->vars, "ERRCODE", SHELL_ERR_PROGRAM);
        shell_set_str_var(&job->vars, "ERRMSG", "job stopped", 11U);
        if (job->script.stop_start != 0U) {
            shell_vm_run_hook(job, job->script.stop_start,
                              job->script.stop_end, verbose);
        } else if (job->script.err_start != 0U) {
            shell_vm_run_hook(job, job->script.err_start,
                              job->script.err_end, verbose);
        }
    } else {
        job->state = JOB_ERROR;
        if (!job->in_handler && job->script.err_start != 0U) {
            shell_vm_run_hook(job, job->script.err_start,
                              job->script.err_end, verbose);
        }
    }

    shell_vm_cleanup(job);
    if (verbose && rc < 0 && job->err_code != 0) {
        shell_err_t err;

        memset(&err, 0, sizeof(err));
        err.code = (int)job->err_code;
        err.line = job->err_line;
        snprintf(err.msg, sizeof(err.msg), "%s", job->err_msg);
        shell_report_error(&err, job->name);
    }
    return rc;
}

/* 同步执行一段脚本（不创建任务） */
int shell_job_run_sync(const char *text, const char *name, bool verbose)
{
    shell_job_t job;
    shell_err_t err;
    int rc;

    memset(&job, 0, sizeof(job));
    shell_vars_init(&job.vars);
    job.used = true;
    job.state = JOB_RUNNING;
    (void)snprintf(job.name, sizeof(job.name), "%s",
                   (name != NULL) ? name : "@interactive");

    if (shell_script_parse(&job.script, text, &err) != 0) {
        shell_report_error(&err, NULL);
        shell_script_free(&job.script);
        return -SHELL_ERR_PARSE;
    }

    rc = shell_vm_run(&job, verbose);
    shell_script_free(&job.script);
    return (rc == 0) ? 0 : -((job.err_code != 0) ? job.err_code : SHELL_ERR_PROGRAM);
}

int shell_job_run_sync_script(shell_job_t *job, bool verbose)
{
    if (job == NULL) {
        return -1;
    }
    job->state = JOB_RUNNING;
    return shell_vm_run(job, verbose);
}

/* ------------------------------ JOB 任务 ------------------------------ */

static void shell_job_task(void *arg)
{
    shell_job_t *job = (shell_job_t *)arg;

    (void)shell_vm_run(job, true);
    (void)iot_sem_give(&job->done);
    iot_task_exit();
}

int shell_job_start(shell_job_t *job)
{
    if (job == NULL || !job->used) {
        return -1;
    }
    switch (job->state) {
    case JOB_LOADED:
    case JOB_COMPLETED:
    case JOB_ERROR:
    case JOB_ABORTED:
        break;
    default:
        return -1;      /* RUNNING / PAUSED / STOPPING */
    }

    shell_job_reap(job);

    job->stop_flag = false;
    job->pc = 0U;
    job->err_code = 0;
    job->err_line = 0U;
    shell_vars_init(&job->vars);
    (void)iot_sem_init(&job->wake, 0U);
    (void)iot_sem_init(&job->done, 0U);

    job->task = iot_task_create_dynamic(job->name, job->prio, shell_job_task,
                                        job, job->stack);
    if (job->task == NULL) {
        return -1;
    }
    job->state = JOB_RUNNING;
    job->has_run = true;
    return 0;
}

int shell_job_stop(shell_job_t *job, bool force)
{
    if (job == NULL || !job->used) {
        return -1;
    }
    if (job->state != JOB_RUNNING && job->state != JOB_PAUSED) {
        return -1;
    }
    job->stop_flag = true;
    job->state = JOB_STOPPING;
    if (job->task != NULL) {
        (void)iot_sem_give(&job->wake);
    }
    if (force && job->task != NULL) {
        /* 强制：置位后由任务在指令边界退出；不执行钩子由 VM 内部的
         * stop_flag 分支决定，这里保持同一路径以保证设备收尾执行 */
        (void)iot_sem_give(&job->wake);
    }
    return 0;
}

int shell_job_wait(shell_job_t *job, uint32_t timeout_ms)
{
    uint32_t wait = (timeout_ms == 0U) ? IOT_TIMEOUT_FOREVER : timeout_ms;

    if (job == NULL) {
        return -1;
    }
    if (job->task == NULL) {
        return 0;       /* 已结束（同步执行） */
    }
    if (iot_sem_take(&job->done, wait) != IOT_OK) {
        return -1;
    }
    return 0;
}
