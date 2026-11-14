#ifndef SHELL_INTERNAL_H
#define SHELL_INTERNAL_H

/*
 * shell_internal.h - Shell 内部数据结构与模块接口（不对外暴露）
 */

#include "shell.h"
#include "ros.h"
#include "common.h"

#include <stdint.h>
#include <stdbool.h>

/* ============================== 值 / 变量 ============================== */

typedef enum {
    SHELL_VAL_INT = 1,
    SHELL_VAL_FLOAT,
    SHELL_VAL_BOOL,
    SHELL_VAL_STRING
} shell_val_type_t;

typedef struct {
    uint8_t type;
    int32_t i;
    float f;
    bool b;
    const char *s;          /* 字符串指向源 token，不拥有内存 */
    uint16_t slen;
} shell_value_t;

typedef struct {
    char name[SHELL_VAR_NAME_MAX];
    uint8_t type;
    bool used;
    bool readonly;
    int32_t i;
    float f;
    bool b;
    char str[SHELL_STR_MAX];
    uint16_t slen;
} shell_var_t;

typedef struct {
    vec_t items;            /* shell_var_t 变长数组（上限 SHELL_VAR_MAX），首次写入时分配 */
} shell_vars_t;

typedef struct {
    int code;
    uint16_t line;
    char msg[48];
} shell_err_t;

/* ============================== token ============================== */

typedef enum {
    SHELL_TOK_EOF = 0,
    SHELL_TOK_WORD,
    SHELL_TOK_STRING,
    SHELL_TOK_NUMBER,
    SHELL_TOK_VAR,
    SHELL_TOK_OP
} shell_tok_type_t;

typedef struct {
    uint8_t type;
    const char *p;
    uint16_t len;
    int32_t ival;
    float fval;
    bool is_float;
} shell_tok_t;

/* ============================== 脚本 ============================== */

typedef enum {
    SHELL_OP_NOP = 0,
    SHELL_OP_SET,
    SHELL_OP_PRINT,
    SHELL_OP_SHELL_PRINT,
    SHELL_OP_SHELL_LOG,
    SHELL_OP_SHELL_STATE,
    SHELL_OP_SHELL_CMD,
    SHELL_OP_IF_FALSE,
    SHELL_OP_JUMP,
    SHELL_OP_FOR_INIT,
    SHELL_OP_FOR_NEXT,
    SHELL_OP_WHILE_CHECK,
    SHELL_OP_BREAK,
    SHELL_OP_CONTINUE,
    SHELL_OP_WAIT_TIME,
    SHELL_OP_START_JOB,
    SHELL_OP_STOP_JOB,
    SHELL_OP_GOTO,
    SHELL_OP_CALL,
    SHELL_OP_RETURN,
    SHELL_OP_ENDJOB
} shell_op_t;

typedef struct {
    uint8_t op;
    uint16_t line;
    uint16_t target;
    uint32_t arg_off;
    uint16_t arg_len;
} shell_instr_t;

typedef struct {
    char *text;
    uint32_t text_len;
    shell_instr_t *code;
    uint16_t count;
    uint16_t err_start;
    uint16_t err_end;
    uint16_t stop_start;
    uint16_t stop_end;
} shell_script_t;

/* ============================== JOB ============================== */

typedef enum {
    JOB_FREE = 0,
    JOB_LOADED,
    JOB_RUNNING,
    JOB_PAUSED,
    JOB_STOPPING,
    JOB_COMPLETED,
    JOB_ERROR,
    JOB_ABORTED
} shell_job_state_t;

typedef struct shell_job {
    bool used;
    uint16_t job_id;
    char name[SHELL_VAR_NAME_MAX];

    shell_t *owner;             /* 所属 Shell 实例 */

    struct shell_job *next;     /* JOB 链表后继（见 shell->jobs） */
    struct shell_job *parent;   /* 启动者：由 START JOB 建立，可为 NULL */

    char *text;                 /* 脚本源（堆） */
    uint32_t text_len;
    shell_script_t script;

    volatile uint8_t state;
    volatile bool stop_flag;
    bool in_handler;            /* 正在执行 ON ERROR / ON STOP，防递归 */
    bool foreground;
    bool has_run;

    uint8_t prio;
    uint32_t stack;

    ros_task_t *task;
    ros_sem_t *wake;            /* 按需分配：stop 唤醒可中断 WAIT */
    ros_sem_t *done;            /* 按需分配：JOB 结束通知 */

    shell_vars_t vars;
    uint16_t pc;
    uint16_t err_line;
    int32_t err_code;
    char err_msg[32];
} shell_job_t;

/* ============================== Shell 实例 ============================== */

struct shell {
    shell_config_t cfg;              /* version / write / user / reset */

    shell_vars_t   vars;             /* Shell 上下文变量表 */
    shell_job_t   *jobs;             /* JOB 链表头（按加载顺序，堆分配） */
    uint16_t       job_seq;

    char          *rx;               /* [SHELL_RX_MAX] */
    uint16_t       rx_head;
    uint16_t       rx_tail;
    volatile bool  ctrlc;

    char          *line;             /* [SHELL_LINE_MAX] */
    uint16_t       line_len;
    bool           last_esc;
    bool           last_cr;          /* 上一个输入是 CR（CRLF 只提交一次） */

    char          *paste;            /* [SHELL_PASTE_MAX] */
    uint16_t       paste_len;

    int            block_depth;
    bool           in_paste;

    bool           exit_req;         /* 平台请求关闭（exit 命令）：任务据此退出 */

    ros_task_t    *task;             /* 输入任务（通常仅默认实例） */
};

/* ============================== shell_expr.c ============================== */

int  shell_lex(const char *text, uint16_t len, shell_tok_t *toks, uint16_t max);
bool shell_ieq(const char *a, uint16_t alen, const char *b);
bool shell_tok_ieq(const shell_tok_t *t, const char *s);
int  shell_expr_eval(const shell_tok_t *toks, uint16_t count,
                     shell_vars_t *vars, shell_value_t *out, shell_err_t *err);
int  shell_value_to_int(const shell_value_t *v, int32_t *out, shell_err_t *err);
void shell_value_format(const shell_value_t *v, char *buf, uint16_t cap,
                        uint16_t *len);

int  shell_vars_init(shell_vars_t *vars);
void shell_vars_clear(shell_vars_t *vars);
void shell_vars_release(shell_vars_t *vars);
shell_var_t *shell_var_find(shell_vars_t *vars, const char *name, uint16_t len);
int  shell_vars_set(shell_vars_t *vars, const char *name, uint16_t nlen,
                    const shell_value_t *val, bool readonly, shell_err_t *err);
int  shell_vars_get(shell_vars_t *vars, const char *name, uint16_t nlen,
                    shell_value_t *out, shell_err_t *err);
int  shell_vars_unset(shell_vars_t *vars, const char *name, uint16_t nlen);

/* ============================== shell_script.c ============================== */

int  shell_script_parse(shell_script_t *script, const char *text,
                        shell_err_t *err);
void shell_script_free(shell_script_t *script);

/* ============================== shell_vm.c ============================== */

int  shell_vm_run(shell_job_t *job, bool verbose);
int  shell_job_run_sync(shell_t *sh, const char *text, const char *name,
                        bool verbose);
int  shell_job_run_sync_script(shell_job_t *job, bool verbose);

shell_job_t *shell_job_find(shell_t *sh, const char *name, uint16_t len);
shell_job_t *shell_job_find_by_task(shell_t *sh, const ros_task_t *task);
/* 链表遍历：从第一个 JOB 开始，按加载顺序逐个返回，末尾为 NULL */
shell_job_t *shell_job_first(shell_t *sh);
shell_job_t *shell_job_next(const shell_job_t *job);

/* 级联到子 JOB（START JOB 启动的后代），目标 JOB 自身不处理。
 * 子 JOB 任务已存在时同步挂起 / 恢复任务，并更新 JOB 状态。 */
void shell_job_pause_descendants(shell_job_t *job);
void shell_job_resume_descendants(shell_job_t *job);
void shell_job_unload(shell_job_t *job);
shell_job_t *shell_job_load(shell_t *sh, const char *text, uint32_t len,
                            const char *name, uint8_t prio, uint32_t stack,
                            shell_err_t *err);
int  shell_job_start(shell_job_t *job);
int  shell_job_stop(shell_job_t *job, bool force);
int  shell_job_wait(shell_job_t *job, uint32_t timeout_ms);
void shell_jobs_clear(shell_t *sh);

/* 是否有 JOB 正在执行（RUNNING / STOPPING）；供平台在回收前查询 */
bool shell_jobs_running(const shell_t *sh);

/* ============================== shell_cmd.c ============================== */

/* 内建命令标志 */
typedef enum {
    CMD_NONE      = 0,
    CMD_HIDDEN    = (1U << 1),   /* help 不列出 */
    CMD_REENTRANT = (1U << 2)    /* 允许被 SHELL <cmd> 透传 */
} shell_cmd_flags_t;

/* 内建命令处理函数：sh 为调用该命令的实例 */
typedef int (*shell_cmd_fn)(shell_t *sh, int argc, char **argv);

typedef struct shell_cmd {
    const char *name;
    shell_cmd_fn handler;
    const char *usage;
    const char *help;
    const char *group;
    uint32_t    flags;
} shell_cmd_t;

int  shell_cmd_dispatch(shell_t *sh, int argc, char **argv);
const shell_cmd_t *shell_cmd_find(const char *name);
void shell_error(shell_t *sh, int code, const char *fmt, ...);

/* ============================== shell.c ============================== */

void shell_report_error(shell_t *sh, const shell_err_t *err, const char *job);
bool shell_is_script_keyword(const char *tok);
bool shell_is_block_start(const char *tok);
const char *shell_job_state_name(uint8_t state);

int  shell_run_paste_buffer(shell_t *sh);
const char *shell_paste_buffer(shell_t *sh, uint32_t *len);
bool shell_ctrl_c_pending(shell_t *sh);
void shell_clear_ctrl_c(shell_t *sh);

#endif /* SHELL_INTERNAL_H */
