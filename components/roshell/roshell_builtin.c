/*
 * roshell_builtin.c - roshell 内建命令实现
 *
 * 内建命令静态表（初始化时登记进全局命令表 map）+ 归属判断 + help 输出。
 * 关键字 / 命令名一律小写、大小写敏感（roshell.md §1）。
 * 会话、行编辑与命令分派在 roshell_context.c；
 * 内置函数 / 常量表在 roshell_func.c。
 */

#include "roshell.h"
#include "roshell_core.h"
#include "roshell_cmd_internal.h"
#include "roshell_context.h"
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
static bool rs_var_text(rs_t *rs, const char *name, uint16_t nlen,
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

static const char *rs_run_state_name(int state)
{
    switch (state) {
    case RS_RUN_RUNNING: return "RUNNING";
    case RS_RUN_DONE:    return "DONE";
    case RS_RUN_ERROR:   return "ERROR";
    default:             return "FREE";
    }
}

/* ============================== 内建命令 ============================== */

int rs_cmd_ver(const rs_cmd_ctx_t *ctx)
{
    char info[512];
    const char *ver;
    int n;

    (void)ctx;
    ver = rs_session_version();
    ros_printf("roshell v1.0\n");
    ros_printf("Build: %s\n", ver != NULL ? ver : "-");
    n = ros_version_format(info, sizeof(info));
    if (n > 0) {
        (void)ros_io_write(NULL, info, (uint32_t)n);
    }
    ros_printf("LINE=%u SCRIPT=%u RUN=%u\n",
               (unsigned)RS_LINE_MAX, (unsigned)RS_SCRIPT_MAX,
               (unsigned)RS_RUN_MAX);
    return 0;
}

int rs_cmd_echo(const rs_cmd_ctx_t *ctx)
{
    int a;
    bool first = true;

    for (a = 1; a < ctx->argc; a++) {
        const char *tok = ctx->argv[a];
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
                if (rs_var_text(ctx->rs, &tok[start], (uint16_t)(end - start),
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

static void rs_ps_run_slots(rs_t *rs)
{
    list_node_t *pos;
    bool any = false;

    ros_printf("run slots:\n");
    if (rs == NULL) {
        ros_printf("  (no instance)\n");
        return;
    }
    /* 活跃槽链表：槽号仍按 slots 下标，顺序为启动先后 */
    for (pos = list_first(&rs->active_list);
         pos != NULL && pos != &rs->active_list.head;
         pos = list_next(pos)) {
        const rs_slot_t *s = ROS_CONTAINER_OF(pos, rs_slot_t, link);
        uint32_t idx = (uint32_t)(s - &rs->slots[0]);

        any = true;
        ros_printf("  %2u  %-16s %s\n", (unsigned)idx,
                   (s->fname[0] != '\0') ? s->fname : "-",
                   rs_run_state_name((int)s->state));
    }
    if (!any) {
        ros_printf("  (none)\n");
    }
}

int rs_cmd_ps(const rs_cmd_ctx_t *ctx)
{
    ros_task_t *task;

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
    rs_ps_run_slots(ctx->rs);
    return 0;
}

int rs_cmd_jobs(const rs_cmd_ctx_t *ctx)
{
    rs_ps_run_slots(ctx->rs);
    return 0;
}

int rs_cmd_waitjob(const rs_cmd_ctx_t *ctx)
{
    int32_t slot = 0;
    uint32_t timeout = RS_SHELL_WAITJOB_MS;
    uint32_t waited = 0U;
    int rc;
    int i;

    if (ctx->argc < 2 || !str_parse_int(ctx->argv[1], &slot)) {
        rs_error(RS_ERR_PARAM, "usage: waitjob <n> [timeout <ms>]");
        return -RS_ERR_PARAM;
    }
    for (i = 2; i + 1 < ctx->argc; i += 2) {
        if (strcmp(ctx->argv[i], "timeout") == 0) {
            uint32_t t;

            if (str_parse_uint(ctx->argv[i + 1], &t)) {
                timeout = t;
            }
        }
    }
    if (ctx->rs == NULL) {
        rs_error(RS_ERR_PARAM, "no roshell instance");
        return -RS_ERR_PARAM;
    }
    for (;;) {
        rc = rs_run_wait(ctx->rs, (int)slot, 100U);
        if (rc != RS_ERR_TIMEOUT) {
            break;
        }
        if (rs_session_pump_ctrlc()) {
            ros_puts("WARN: wait interrupted");
            return 0;
        }
        waited += 100U;
        if (timeout != 0U && waited >= timeout) {
            rs_error(RS_ERR_TIMEOUT, "waitjob %d timeout", (int)slot);
            return -RS_ERR_TIMEOUT;
        }
    }
    if (rc == RS_OK) {
        return 0;
    }
    if (rc == RS_ERR_PARAM) {
        rs_error(RS_ERR_NOT_FOUND, "slot %d not found", (int)slot);
        return -RS_ERR_NOT_FOUND;
    }
    rs_error(rc, "run %d failed", (int)slot);
    return rc;
}

/* ---------------- wait 可暂停登记表 ---------------- */
/*
 * wait <ms> 以 RS_WAIT_SLICE 分片延时并登记到会话上下文的 waits（vec）：
 *   on_pause  记录剩余时间 remain 并置 paused，任务在分片边界进入停放
 *             循环（暂停期间不推进 deadline，即不消耗 wait 时间）；
 *   on_resume 以 now + remain 重设 deadline 并解除停放；
 *   on_kill   清表项，停放中的任务在下一分片边界退出等待。
 * pause 命令命中停放（rs_wait_is_parked）后不再做 OS 级挂起。
 * 登记表按 RS_WAIT_SLOTS 预分配、不扩容，保证 pause/kill 侧持有的
 * 表项指针不会因 realloc 失效。
 */
#define RS_WAIT_SLOTS   4U
#define RS_WAIT_SLICE   50U

typedef struct {
    ros_task_t *task;
    uint32_t deadline;      /* 本轮等待截止 tick */
    uint32_t remain;        /* 暂停时记录的剩余毫秒 */
    bool     paused;
    bool     used;
} rs_wait_t;

/* 会话 wait 登记表（vec 位于 rs_context_t，首次使用时按容量预分配） */
static vec_t *rs_wait_vec(void)
{
    vec_t *v = rs_session_waits();

    if (v->elem_size == 0U &&
        vec_init(v, sizeof(rs_wait_t), RS_WAIT_SLOTS) != 0) {
        return NULL;
    }
    return v;
}

static rs_wait_t *rs_wait_find(const ros_task_t *task)
{
    vec_t *v = rs_session_waits();
    uint32_t i;

    for (i = 0U; i < vec_size(v); i++) {
        rs_wait_t *w = (rs_wait_t *)vec_at(v, i);

        if (w != NULL && w->used && w->task == task) {
            return w;
        }
    }
    return NULL;
}

/* 任务当前是否处于 wait 停放态（pause 命令据此跳过 OS 挂起） */
static bool rs_wait_is_parked(const ros_task_t *task)
{
    const rs_wait_t *w = rs_wait_find(task);

    return (w != NULL) && w->paused;
}

static int rs_wait_on_pause(void *user, ros_task_t *task)
{
    rs_wait_t *w;
    uint32_t now;

    (void)user;
    w = rs_wait_find(task);
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

static int rs_wait_on_resume(void *user, ros_task_t *task)
{
    rs_wait_t *w;

    (void)user;
    w = rs_wait_find(task);
    if (w == NULL) {
        return -1;
    }
    w->deadline = ros_tick_get() + w->remain;
    w->paused = false;
    return 0;
}

static int rs_wait_on_kill(void *user, ros_task_t *task)
{
    rs_wait_t *w;

    (void)user;
    w = rs_wait_find(task);
    if (w == NULL) {
        return 0;
    }
    w->paused = false;
    w->used = false;
    w->task = NULL;
    return 0;
}

/* wait <ms>：可暂停延时当前任务（规格 §9；取代旧 wait time 语句）。
 * 分片循环至 deadline；暂停期间停放不消耗时间，恢复后只等剩余时间。 */
int rs_cmd_wait(const rs_cmd_ctx_t *ctx)
{
    uint32_t ms = 0U;
    rs_wait_t *w = NULL;
    uint32_t i;

    if (ctx->argc < 2 || !str_parse_uint(ctx->argv[1], &ms)) {
        rs_error(RS_ERR_PARAM, "usage: wait <ms>");
        return -RS_ERR_PARAM;
    }
    if (ros_current_task != NULL) {
        vec_t *v = rs_wait_vec();

        if (v != NULL) {
            for (i = 0U; i < vec_size(v); i++) {
                rs_wait_t *e = (rs_wait_t *)vec_at(v, i);

                if (!e->used) {
                    w = e;
                    break;
                }
            }
            /* 空槽用尽时追加一个（容量已预分配，不会 realloc） */
            if (w == NULL && vec_size(v) < RS_WAIT_SLOTS &&
                vec_resize(v, vec_size(v) + 1U) == 0) {
                w = (rs_wait_t *)vec_at(v, vec_size(v) - 1U);
            }
        }
    }
    if (w == NULL) {
        ros_sleep_ms(ms);               /* 表满/无任务上下文：退化为不可暂停延时 */
        return 0;
    }
    w->task = ros_current_task;
    w->deadline = ros_tick_get() + ms;
    w->remain = 0U;
    w->paused = false;
    w->used = true;                     /* used 最后置位，保证表项内容完整可见 */

    for (;;) {
        uint32_t now = ros_tick_get();

        if (!w->used || w->task != ros_current_task) {
            break;                      /* kill 清表（或表项已被复用）：提前退出 */
        }
        if (w->paused) {
            ros_sleep_ms(RS_WAIT_SLICE);    /* 停放：不推进 deadline */
            continue;
        }
        if (!ROS_TICK_AFTER(w->deadline, now)) {
            break;                          /* 正常到期 */
        }
        {
            uint32_t left = w->deadline - now;

            ros_sleep_ms((left > RS_WAIT_SLICE) ? RS_WAIT_SLICE : left);
        }
    }
    if (w->used && w->task == ros_current_task) {
        w->used = false;
        w->task = NULL;
    }
    return 0;
}

/* run <fn> [args...]：异步执行已登记函数（规格 §9）；
 * 实参为字面量或 $var（命令层替换，不做表达式求值）。
 * 注意：函数名避开命令表 API 导出的 rs_cmd_run。 */
int rs_cmd_runjob(const rs_cmd_ctx_t *ctx)
{
    rs_arg_t args[RS_FUNC_ARGS_MAX];
    char sbuf[RS_FUNC_ARGS_MAX][RS_STR_MAX];
    int nargs;
    int slot;
    int i;

    if (ctx->rs == NULL) {
        rs_error(RS_ERR_PARAM, "no roshell instance");
        return -RS_ERR_PARAM;
    }
    if (ctx->argc < 2) {
        rs_error(RS_ERR_PARAM, "usage: run <fn> [args...]");
        return -RS_ERR_PARAM;
    }
    nargs = ctx->argc - 2;
    if (nargs > (int)RS_FUNC_ARGS_MAX) {
        rs_error(RS_ERR_LIMIT, "too many arguments");
        return -RS_ERR_LIMIT;
    }
    (void)memset(args, 0, sizeof(args));
    for (i = 0; i < nargs; i++) {
        const char *a = ctx->argv[2 + i];
        rs_arg_t *out = &args[i];

        if (a[0] == '$' && a[1] != '\0') {
            int32_t iv;
            float fv;
            bool bv;

            if (rs_get_int(ctx->rs, a + 1, &iv) == RS_OK) {
                out->type = RS_ARG_INT;
                out->i = iv;
            } else if (rs_get_float(ctx->rs, a + 1, &fv) == RS_OK) {
                out->type = RS_ARG_FLOAT;
                out->f = fv;
            } else if (rs_get_bool(ctx->rs, a + 1, &bv) == RS_OK) {
                out->type = RS_ARG_BOOL;
                out->b = bv;
            } else if (rs_get_str(ctx->rs, a + 1, sbuf[i],
                                       RS_STR_MAX) == RS_OK) {
                out->type = RS_ARG_STR;
                out->s = sbuf[i];
                out->slen = (uint16_t)strlen(sbuf[i]);
            } else {
                rs_error(RS_ERR_UNDEF_VAR, "undefined variable %s", a);
                return -RS_ERR_UNDEF_VAR;
            }
        } else if (a[0] == '"') {
            const char *body = a + 1;
            size_t len = strlen(body);

            if (len > 0U && body[len - 1U] == '"') {
                len--;
            }
            if (len >= RS_STR_MAX) {
                len = RS_STR_MAX - 1U;
            }
            (void)memcpy(sbuf[i], body, len);
            sbuf[i][len] = '\0';
            out->type = RS_ARG_STR;
            out->s = sbuf[i];
            out->slen = (uint16_t)len;
        } else if (strcmp(a, "true") == 0 || strcmp(a, "false") == 0) {
            out->type = RS_ARG_BOOL;
            out->b = (a[4] == 'u');
        } else {
            int32_t iv;
            float fv;

            if (str_parse_int(a, &iv)) {
                out->type = RS_ARG_INT;
                out->i = iv;
            } else if (str_parse_float(a, &fv)) {
                out->type = RS_ARG_FLOAT;
                out->f = fv;
            } else {
                rs_error(RS_ERR_PARAM, "bad run argument: %s", a);
                return -RS_ERR_PARAM;
            }
        }
    }
    slot = rs_run_async(ctx->rs, ctx->argv[1], (nargs > 0) ? args : NULL, nargs);
    if (slot < 0) {
        rs_error(slot, "run %s failed", ctx->argv[1]);
        return slot;
    }
    ros_printf("job slot %d started\n", slot);
    return 0;
}

/* 会话任务 / roshell run 任务 / idle / 自身不允许 kill */
static bool rs_task_protected(const ros_task_t *task)
{
    return (task == ros_current_task) || (task == ros_idle_task) ||
           (task == rs_session_task()) ||
           (task->name != NULL && strcmp(task->name, "roshell") == 0);
}

int rs_cmd_kill(const rs_cmd_ctx_t *ctx)
{
    ros_task_t *task;

    if (ctx->argc < 2) {
        rs_error(RS_ERR_PARAM, "usage: kill <name|#tid>");
        return -RS_ERR_PARAM;
    }
    task = rs_find_task(ctx->argv[1]);
    if (task == NULL) {
        rs_error(RS_ERR_NOT_FOUND, "task %s not found", ctx->argv[1]);
        return -RS_ERR_NOT_FOUND;
    }
    if (rs_task_protected(task)) {
        rs_error(RS_ERR_PARAM, "refusing to kill this task");
        return -RS_ERR_PARAM;
    }
    (void)rs_cmd_notify_kill(task);
    if (ros_task_destroy(task) != ROS_OK) {
        rs_error(RS_ERR_PARAM, "task busy or static");
        return -RS_ERR_PARAM;
    }
    ros_printf("task %s deleted\n", ctx->argv[1]);
    return 0;
}

int rs_cmd_pause(const rs_cmd_ctx_t *ctx)
{
    ros_task_t *task;
    int rc;

    if (ctx->argc < 2) {
        rs_error(RS_ERR_PARAM, "usage: pause <name|#tid>");
        return -RS_ERR_PARAM;
    }
    task = rs_find_task(ctx->argv[1]);
    if (task == NULL) {
        rs_error(RS_ERR_NOT_FOUND, "task %s not found", ctx->argv[1]);
        return -RS_ERR_NOT_FOUND;
    }
    if (task == ros_current_task || task == ros_idle_task ||
        task == rs_session_task()) {
        rs_error(RS_ERR_PARAM, "refusing to pause this task");
        return -RS_ERR_PARAM;
    }
    /* 双路：先通知命令钩子。wait 钩子命中（任务进入停放）则不做
     * OS 挂起；钩子返回 -1（未登记）回退 OS 挂起，其它负值拒绝暂停。 */
    rc = rs_cmd_notify_pause(task);
    if (rs_wait_is_parked(task)) {
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

int rs_cmd_resume(const rs_cmd_ctx_t *ctx)
{
    ros_task_t *task;

    if (ctx->argc < 2) {
        rs_error(RS_ERR_PARAM, "usage: resume <name|#tid>");
        return -RS_ERR_PARAM;
    }
    task = rs_find_task(ctx->argv[1]);
    if (task == NULL) {
        rs_error(RS_ERR_NOT_FOUND, "task %s not found", ctx->argv[1]);
        return -RS_ERR_NOT_FOUND;
    }
    /* 双路：wait 停放态没有 OS 挂起，直接通知钩子解除停放 */
    if (rs_wait_is_parked(task)) {
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

int rs_cmd_mem(const rs_cmd_ctx_t *ctx)
{
    (void)ctx;
    rs_mem_line();
    return 0;
}

int rs_cmd_state(const rs_cmd_ctx_t *ctx)
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

    if (ctx->rs != NULL) {
        list_node_t *pos;

        for (pos = list_first(&ctx->rs->active_list);
             pos != NULL && pos != &ctx->rs->active_list.head;
             pos = list_next(pos)) {
            const rs_slot_t *s = ROS_CONTAINER_OF(pos, rs_slot_t, link);

            if (s->state == RS_RUN_RUNNING) {
                run_busy++;
            }
        }
    }
    ros_printf("run slots : %u running / %u total\n",
               (unsigned)run_busy, (unsigned)RS_RUN_MAX);
    return 0;
}

int rs_cmd_exit(const rs_cmd_ctx_t *ctx)
{
    (void)ctx;
    ros_puts("bye");
    rs_shell_request_exit();
    return 0;
}

/* ------------------------------ help ------------------------------ */

static const rs_cmd_t s_cmds[] = {
    { "help",    NULL, "help [cmd]",                 "basic",   NULL, 0U,
      rs_builtin_help,    NULL, NULL, NULL, NULL },
    { "ver",     NULL, "ver",                        "basic",   NULL, 0U,
      rs_cmd_ver,     NULL, NULL, NULL, NULL },
    { "echo",    NULL, "echo <text|$var>...",        "basic",   NULL, 0U,
      rs_cmd_echo,    NULL, NULL, NULL, NULL },
    { "ps",      NULL, "ps",                         "task",    NULL, 0U,
      rs_cmd_ps,      NULL, NULL, NULL, NULL },
    { "kill",    NULL, "kill <name|#tid>",           "task",    NULL, 0U,
      rs_cmd_kill,    NULL, NULL, NULL, NULL },
    { "pause",   NULL, "pause <name|#tid>",          "task",    NULL, 0U,
      rs_cmd_pause,   NULL, NULL, NULL, NULL },
    { "resume",  NULL, "resume <name|#tid>",         "task",    NULL, 0U,
      rs_cmd_resume,  NULL, NULL, NULL, NULL },
    { "mem",     NULL, "mem",                        "sys",     NULL, 0U,
      rs_cmd_mem,     NULL, NULL, NULL, NULL },
    { "state",   NULL, "state",                      "sys",     NULL, 0U,
      rs_cmd_state,   NULL, NULL, NULL, NULL },
    { "jobs",    NULL, "jobs",                       "run",     NULL, 0U,
      rs_cmd_jobs,    NULL, NULL, NULL, NULL },
    { "waitjob", NULL, "waitjob <n> [timeout <ms>]", "run",     NULL, 0U,
      rs_cmd_waitjob, NULL, NULL, NULL, NULL },
    { "run",     NULL, "run <fn> [args...]",         "run",     NULL, 0U,
      rs_cmd_runjob,  NULL, NULL, NULL, NULL },
    { "wait",    NULL, "wait <ms>",                  "run",     NULL, 0U,
      rs_cmd_wait,    NULL, rs_wait_on_pause, rs_wait_on_resume,
      rs_wait_on_kill },
    { "exit",    NULL, "exit",                       "session", NULL, 0U,
      rs_cmd_exit,    NULL, NULL, NULL, NULL }
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

int rs_builtin_help(const rs_cmd_ctx_t *ctx)
{
    uint32_t i;
    int argc = ctx->argc;
    char **argv = ctx->argv;

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
