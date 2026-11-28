/*
 * roshell_cmd.c - roshell 全局命令表与命令行分派
 *
 * 命令 usage / help 文本生成、全局命令表注册 / 查找 / 运行、
 * 运行中命令状态机（on_start / on_tick / on_stop，含 '-' 异步持续命令）、
 * 命令行分派（块内 hook / 交互分派）与会话查询。参数解析在
 * roshell_args.c，会话与行编辑在 roshell.c。
 */

#include "roshell.h"
#include "ros.h"
#include "common.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ============ 命令表（原 cmd 组件） ============ *//*
 * rs_cmd_t 自包含地描述命令（名称 / 说明 / 参数表 / 处理函数 / 上下文 /
 * 生命周期钩子），帮助文本由参数表动态生成；
 * 参数解析由 rs_args.c 提供；rs_cmd_run() 按 argv[0] 查表、解析并调用处理函数。
 * rs_cmd_usage() / rs_cmd_help() 生成可直接打印的用法与帮助文本。
 */

static int rs_cmd_append(char *buf, uint32_t size, int off, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (off < 0) {
        return -1;
    }
    va_start(ap, fmt);
    n = str_vsnprintf(&buf[off], size - (uint32_t)off, fmt, ap);
    va_end(ap);
    if (n < 0 || (uint32_t)n >= size - (uint32_t)off) {
        return -1;
    }
    return off + n;
}

static const char *rs_cmd_type_placeholder(uint8_t type)
{
    switch (type) {
    case RS_ARGS_TYPE_INT:
    case RS_ARGS_TYPE_UINT:
        return "<n>";
    case RS_ARGS_TYPE_FLOAT:
        return "<f>";
    default:
        return "<s>";
    }
}

int rs_cmd_usage(const rs_cmd_t *def, char *buf, uint32_t size)
{
    int off = 0;
    uint8_t i;

    if (def == NULL || buf == NULL || size == 0U) {
        return -1;
    }
    if (def->usage != NULL) {
        return rs_cmd_append(buf, size, off, "%s", def->usage);
    }
    off = rs_cmd_append(buf, size, off, "%s", def->name);
    for (i = 0U; i < def->arg_count; i++) {
        const rs_args_t *a = &def->args[i];

        if (a->positional) {
            off = rs_cmd_append(buf, size, off, a->required ? " <%s>" : " [%s]",
                             (a->name != NULL) ? a->name : "arg");
        } else if (a->type == RS_ARGS_TYPE_BOOL) {
            off = rs_cmd_append(buf, size, off, a->required ? " --%s" : " [--%s]",
                             a->name);
        } else {
            off = rs_cmd_append(buf, size, off,
                             a->required ? " --%s %s" : " [--%s %s]",
                             a->name, rs_cmd_type_placeholder(a->type));
        }
    }
    return off;
}

int rs_cmd_help(const rs_cmd_t *def, char *buf, uint32_t size)
{
    int off = 0;
    uint8_t i;

    if (def == NULL || buf == NULL || size == 0U) {
        return -1;
    }
    off = rs_cmd_append(buf, size, off, "%s - %s\n", def->name,
                     (def->desc != NULL) ? def->desc : "");
    off = rs_cmd_append(buf, size, off, "usage: ");
    {
        int n = rs_cmd_usage(def, &buf[off], size - (uint32_t)off);

        if (n < 0) {
            return -1;
        }
        off += n;
    }
    off = rs_cmd_append(buf, size, off, "\n");
    off = rs_cmd_append(buf, size, off, "arguments:\n");
    for (i = 0U; i < def->arg_count; i++) {
        const rs_args_t *a = &def->args[i];

        if (a->positional) {
            off = rs_cmd_append(buf, size, off, "  %-14s %s\n", a->name,
                             (a->desc != NULL) ? a->desc : "");
        } else if (a->short_name != '\0') {
            off = rs_cmd_append(buf, size, off, "  --%s, -%c  %s\n", a->name,
                             a->short_name, (a->desc != NULL) ? a->desc : "");
        } else {
            off = rs_cmd_append(buf, size, off, "  --%s  %s\n", a->name,
                             (a->desc != NULL) ? a->desc : "");
        }
        if (a->def != NULL) {
            off = rs_cmd_append(buf, size, off, "                 default: %s\n",
                             a->def);
        }
    }
    return off;
}

int rs_cmd_run(roshell_t *rs, const rs_cmd_t *table, uint32_t count,
            int argc, char **argv, void *user)
{
    rs_args_parsed_t parsed;
    const rs_args_parsed_t *pparsed;
    uint32_t i;
    int rc;

    if (table == NULL || argc < 1 || argv == NULL || argv[0] == NULL) {
        return RS_ARGS_ERR_PARAM;
    }
    for (i = 0U; i < count; i++) {
        if (table[i].name != NULL && str_icmp(table[i].name, argv[0])) {
            pparsed = NULL;
            if (table[i].args != NULL) {
                rc = rs_args_parse(table[i].args, table[i].arg_count,
                               &argv[1], argc - 1, &parsed);
                if (rc != RS_ARGS_OK) {
                    return rc;
                }
                pparsed = &parsed;
            }
            if (table[i].on_start != NULL) {
                return rs_cmd_execute(rs, &table[i], argc, argv, pparsed,
                                      user, true);
            }
            return RS_ARGS_OK;
        }
    }
    return RS_ARGS_ERR_UNKNOWN;
}

/* ============================== 全局命令表 ============================== */
/*
 * 内建命令与扩展命令共用一张 map：名称 -> const rs_cmd_t *。
 * 首次访问时惰性初始化，并把内建静态表一次性登记进来；
 * 键由 map 拷贝，查找区分大小写（与 roshell.md 的“命令名全小写”一致）。
 */

map_t rs_cmd_table;

static bool s_commands_ready;

/* 登记一条命令；名称已存在（含内建命令）或参数非法返回 false */
static bool rs_cmd_add(const rs_cmd_t *def)
{
    if (def == NULL || def->name == NULL || def->on_start == NULL) {
        return false;
    }
    if (map_contains(&rs_cmd_table, def->name)) {
        return false;
    }
    return map_put(&rs_cmd_table, def->name, (void *)def);
}

/* 把内建命令全部登记到命令表（调用方持有临界区） */
static void rs_cmd_add_builtins(void)
{
    const rs_cmd_t *builtins;
    uint32_t count = 0U;
    uint32_t i;

    builtins = rs_builtin_table(&count);
    for (i = 0U; i < count; i++) {
        (void)rs_cmd_add(&builtins[i]);
    }
}

static void rs_cmd_commands_ensure(void)
{
    ros_port_critical_enter();
    if (!s_commands_ready) {
        if (map_init(&rs_cmd_table) == 0) {
            s_commands_ready = true;
            rs_cmd_add_builtins();
        }
    }
    ros_port_critical_exit();
}

int rs_cmd_register(const rs_cmd_t *def)
{
    bool ok;

    if (def == NULL || def->name == NULL || def->on_start == NULL) {
        return -1;
    }
    rs_cmd_commands_ensure();
    if (!s_commands_ready) {
        return -1;                  /* 命令表初始化失败 */
    }
    /* 查重 + 入表在同一临界区，避免并发注册产生重复项 */
    ros_port_critical_enter();
    ok = rs_cmd_add(def);
    ros_port_critical_exit();
    return ok ? 0 : -1;
}

void rs_cmd_unregister_all(void)
{
    rs_cmd_commands_ensure();
    ros_port_critical_enter();
    if (s_commands_ready) {
        map_clear(&rs_cmd_table);
        rs_cmd_add_builtins();
    }
    ros_port_critical_exit();
}

const rs_cmd_t *rs_cmd_find_command(const char *name)
{
    const rs_cmd_t *def = NULL;

    if (name == NULL) {
        return NULL;
    }
    rs_cmd_commands_ensure();
    ros_port_critical_enter();
    if (s_commands_ready) {
        def = (const rs_cmd_t *)map_get_ptr(&rs_cmd_table, name);
    }
    ros_port_critical_exit();
    return def;
}

const rs_cmd_t *rs_cmd_first(void)
{
    map_iter_t it;
    map_item_t item;
    const rs_cmd_t *def = NULL;

    rs_cmd_commands_ensure();
    ros_port_critical_enter();
    if (s_commands_ready) {
        map_iter_begin(&rs_cmd_table, &it);
        if (map_iter_next(&it, &item)) {
            def = (const rs_cmd_t *)item.value;
        }
    }
    ros_port_critical_exit();
    return def;
}

const rs_cmd_t *rs_cmd_next(const rs_cmd_t *def)
{
    map_iter_t it;
    map_item_t item;
    const rs_cmd_t *next = NULL;

    if (def == NULL) {
        return NULL;
    }
    rs_cmd_commands_ensure();
    ros_port_critical_enter();
    if (s_commands_ready) {
        map_iter_begin(&rs_cmd_table, &it);
        while (map_iter_next(&it, &item)) {
            if ((const rs_cmd_t *)item.value == def) {
                if (map_iter_next(&it, &item)) {
                    next = (const rs_cmd_t *)item.value;
                }
                break;
            }
        }
    }
    ros_port_critical_exit();
    return next;
}

/* ============================== 运行中命令登记 ============================== */

/* 轮转周期：shell 任务 / 等待方调用 on_tick 的间隔 */
#define RS_CMD_TICK_MS      1U

#define RS_CMD_F_RUNNING    0x01U   /* 持续命令尚未结束 */
#define RS_CMD_F_ASYNC      0x02U   /* '-' 启动，不阻塞调用方 */

/* 记录“某任务正在执行哪条命令、属于哪个实例、处于什么状态”，
 * 供 pause/resume/stop 与 shell 轮转 tick 使用 */
typedef struct {
    ros_task_t *task;
    const rs_cmd_t *def;
    roshell_t *rs;              /* 命令所属实例（持续命令状态存放处） */
    uint32_t flags;             /* RS_CMD_F_* */
} rs_cmd_active_t;

static vec_t s_active;
static bool s_active_ready;
static uint32_t s_tick_cursor;

/* 调用方需持有临界区 */
static bool rs_active_ensure(void)
{
    if (!s_active_ready) {
        if (vec_init(&s_active, sizeof(rs_cmd_active_t), 4U) != 0) {
            return false;
        }
        s_active_ready = true;
    }
    return true;
}

/* 调用方需持有临界区：返回指定项的索引，找不到为 -1 */
static int rs_active_index(ros_task_t *task, const rs_cmd_t *def)
{
    uint32_t n = vec_size(&s_active);

    while (n > 0U) {
        const rs_cmd_active_t *e;

        n--;
        e = (const rs_cmd_active_t *)vec_at(&s_active, n);
        if (e->task == task && e->def == def) {
            return (int)n;
        }
    }
    return -1;
}

/* 登记一条正在执行的命令；失败返回 false */
static bool rs_active_add(ros_task_t *task, const rs_cmd_t *def,
                          roshell_t *rs, bool async)
{
    rs_cmd_active_t item;
    bool ok = false;

    ros_port_critical_enter();
    if (rs_active_ensure()) {
        item.task = task;
        item.def = def;
        item.rs = rs;
        item.flags = async ? RS_CMD_F_ASYNC : 0U;
        ok = (vec_push(&s_active, &item) == 0);
    }
    ros_port_critical_exit();
    return ok;
}

static void rs_active_set_running(ros_task_t *task, const rs_cmd_t *def)
{
    ros_port_critical_enter();
    {
        int idx = rs_active_index(task, def);

        if (idx >= 0) {
            ((rs_cmd_active_t *)vec_at(&s_active, (uint32_t)idx))->flags |=
                RS_CMD_F_RUNNING;
        }
    }
    ros_port_critical_exit();
}

/* 移除指定项；out_rs 非空时写回该项所属实例 */
static bool rs_active_remove(ros_task_t *task, const rs_cmd_t *def,
                             roshell_t **out_rs)
{
    bool found = false;

    ros_port_critical_enter();
    {
        int idx = rs_active_index(task, def);

        if (idx >= 0) {
            if (out_rs != NULL) {
                *out_rs = ((const rs_cmd_active_t *)
                           vec_at(&s_active, (uint32_t)idx))->rs;
            }
            (void)vec_remove(&s_active, (uint32_t)idx, NULL);
            found = true;
        }
    }
    ros_port_critical_exit();
    return found;
}

/* 查找任务当前命令；out_rs 非空时写回所属实例 */
static const rs_cmd_t *rs_cmd_active_find(const ros_task_t *task,
                                          roshell_t **out_rs)
{
    const rs_cmd_t *found = NULL;
    uint32_t i;

    if (task == NULL) {
        return NULL;
    }
    ros_port_critical_enter();
    i = vec_size(&s_active);
    while (i > 0U) {
        const rs_cmd_active_t *item =
            (const rs_cmd_active_t *)vec_at(&s_active, i - 1U);

        if (item->task == task) {
            found = item->def;
            if (out_rs != NULL) {
                *out_rs = item->rs;
            }
            break;
        }
        i--;
    }
    ros_port_critical_exit();
    return found;
}

static void rs_cmd_active_remove_task(const ros_task_t *task)
{
    uint32_t i = 0U;

    ros_port_critical_enter();
    while (i < vec_size(&s_active)) {
        const rs_cmd_active_t *item = (const rs_cmd_active_t *)vec_at(&s_active, i);

        if (item->task == task) {
            (void)vec_remove(&s_active, i, NULL);
        } else {
            i++;
        }
    }
    ros_port_critical_exit();
}

/* 命令结束：移除登记并调用一次 on_stop（正常结束与被杀统一走这里） */
static void rs_cmd_finish(ros_task_t *task, const rs_cmd_t *def)
{
    roshell_t *rs = NULL;

    if (rs_active_remove(task, def, &rs) && def->on_stop != NULL) {
        (void)def->on_stop(rs, def->user);
    }
}

bool rs_cmd_has_async(roshell_t *rs)
{
    bool found = false;
    uint32_t i;

    ros_port_critical_enter();
    for (i = 0U; i < vec_size(&s_active); i++) {
        const rs_cmd_active_t *e = (const rs_cmd_active_t *)vec_at(&s_active, i);

        if (e->rs != rs) {
            continue;
        }
        if ((e->flags & (RS_CMD_F_RUNNING | RS_CMD_F_ASYNC)) ==
            (RS_CMD_F_RUNNING | RS_CMD_F_ASYNC)) {
            found = true;
            break;
        }
    }
    ros_port_critical_exit();
    return found;
}

/* 是否存在匹配的异步持续命令（name 为 NULL 表示任意；self 用于排除 wait 自身） */
static bool rs_cmd_has_async_match(roshell_t *rs, const rs_cmd_t *self,
                                   const char *name)
{
    bool found = false;
    uint32_t i;

    ros_port_critical_enter();
    for (i = 0U; i < vec_size(&s_active); i++) {
        const rs_cmd_active_t *e = (const rs_cmd_active_t *)vec_at(&s_active, i);

        if (e->rs != rs) {
            continue;
        }
        if ((e->flags & (RS_CMD_F_RUNNING | RS_CMD_F_ASYNC)) !=
            (RS_CMD_F_RUNNING | RS_CMD_F_ASYNC)) {
            continue;
        }
        if (e->def == self) {
            continue;
        }
        if (name != NULL &&
            (e->def->name == NULL || strcmp(e->def->name, name) != 0)) {
            continue;
        }
        found = true;
        break;
    }
    ros_port_critical_exit();
    return found;
}

/*
 * 执行一条命令（统一入口）：
 *   wait=true  同步执行，持续命令在此轮转 on_tick 直到结束；
 *   wait=false '-' 异步启动，on_start 返回 RS_CMD_RUNNING 后立即返回，
 *              后续由 rs_cmd_tick_all()（shell 任务）继续推进。
 * 返回命令结束码；异步启动返回 RS_CMD_RUNNING。
 */
int rs_cmd_execute(roshell_t *rs, const rs_cmd_t *def, int argc, char **argv,
                   const rs_args_parsed_t *parsed, void *default_user,
                   bool wait)
{
    ros_task_t *self = ros_current_task;
    void *user;
    int rc;

    if (def == NULL || def->on_start == NULL) {
        return RS_ARGS_ERR_PARAM;
    }
    user = (def->user != NULL) ? def->user : default_user;
    if (!rs_active_add(self, def, rs, !wait)) {
        return RS_ERR_RESOURCE;
    }

    rc = def->on_start(rs, argc, argv, parsed, user);
    if (rc != RS_CMD_RUNNING) {
        rs_cmd_finish(self, def);           /* 同步结束 */
        return rc;
    }
    if (def->on_tick == NULL) {
        rs_cmd_finish(self, def);
        return RS_ERR_UNSUPPORTED;          /* 声明为持续命令却没有 tick */
    }
    rs_active_set_running(self, def);
    if (!wait) {
        return RS_CMD_RUNNING;              /* 异步：交给 shell 轮转 */
    }

    for (;;) {
        rc = def->on_tick(rs, def->user);
        if (rc != RS_CMD_RUNNING) {
            break;
        }
        ros_sleep_ms(RS_CMD_TICK_MS);
    }
    rs_cmd_finish(self, def);
    return rc;
}

/* shell 任务每轮调用：推进本实例的一个异步持续命令（多命令时按 cursor 轮转） */
void rs_cmd_tick_all(roshell_t *rs)
{
    ros_task_t *task = NULL;
    const rs_cmd_t *def = NULL;
    int rc;

    ros_port_critical_enter();
    {
        uint32_t n = vec_size(&s_active);
        uint32_t k;

        for (k = 0U; k < n; k++) {
            uint32_t idx = (s_tick_cursor + k) % n;
            const rs_cmd_active_t *e =
                (const rs_cmd_active_t *)vec_at(&s_active, idx);

            if (e->rs != rs) {
                continue;
            }
            if ((e->flags & (RS_CMD_F_RUNNING | RS_CMD_F_ASYNC)) ==
                (RS_CMD_F_RUNNING | RS_CMD_F_ASYNC)) {
                task = e->task;
                def = e->def;
                s_tick_cursor = idx + 1U;
                break;
            }
        }
    }
    ros_port_critical_exit();

    if (def == NULL) {
        return;
    }
    rc = def->on_tick(rs, def->user);
    if (rc != RS_CMD_RUNNING) {
        rs_cmd_finish(task, def);
    }
}

/* 等待异步持续命令结束（wait 命令使用）；timeout_ms=0 表示无限等待 */
int rs_cmd_wait_async(roshell_t *rs, const rs_cmd_t *self, const char *name,
                      uint32_t timeout_ms)
{
    uint32_t start = ros_tick_get();

    for (;;) {
        if (!rs_cmd_has_async_match(rs, self, name)) {
            return 0;
        }
        if (timeout_ms != 0U &&
            (uint32_t)(ros_tick_get() - start) >= timeout_ms) {
            return -RS_ERR_TIMEOUT;
        }
        rs_cmd_tick_all(rs);
        ros_sleep_ms(RS_CMD_TICK_MS);
    }
}

int rs_cmd_notify_pause(const ros_task_t *task)
{
    roshell_t *rs = NULL;
    const rs_cmd_t *def = rs_cmd_active_find(task, &rs);

    if (def == NULL || def->on_pause == NULL) {
        return 0;
    }
    return def->on_pause(rs, def->user);
}

int rs_cmd_notify_resume(const ros_task_t *task)
{
    roshell_t *rs = NULL;
    const rs_cmd_t *def = rs_cmd_active_find(task, &rs);

    if (def == NULL || def->on_resume == NULL) {
        return 0;
    }
    return def->on_resume(rs, def->user);
}

int rs_cmd_notify_kill(const ros_task_t *task)
{
    roshell_t *rs = NULL;
    const rs_cmd_t *def = rs_cmd_active_find(task, &rs);
    int rc = 0;

    /* 先移除登记，避免 on_stop 之后仍被 tick_all 推进 */
    rs_cmd_active_remove_task(task);
    if (def != NULL && def->on_stop != NULL) {
        rc = def->on_stop(rs, def->user);
    }
    return rc;
}

int rs_cmd_exec_node(roshell_t *rs, const rs_cmd_t *def, int argc,
                     char **argv, bool wait)
{
    rs_args_parsed_t parsed;
    const rs_args_parsed_t *pparsed = NULL;
    int rc;

    if (def == NULL || def->on_start == NULL ||
        argc < 0 || (argv == NULL && argc != 0)) {
        return RS_ARGS_ERR_PARAM;
    }
    if (def->args != NULL) {
        rc = rs_args_parse(def->args, def->arg_count, argv, argc, &parsed);
        if (rc != RS_ARGS_OK) {
            return rc;
        }
        pparsed = &parsed;
    }
    return rs_cmd_execute(rs, def, argc, argv, pparsed, NULL, wait);
}

int rs_cmd_run_node(roshell_t *rs, const rs_cmd_t *def, int argc, char **argv)
{
    return rs_cmd_exec_node(rs, def, argc, argv, true);
}

int rs_builtin_dispatch(roshell_t *rs, int argc, char **argv)
{
    const rs_cmd_t *def;

    if (argc < 1 || argv == NULL || argv[0] == NULL) {
        return RS_ARGS_ERR_PARAM;
    }
    def = rs_cmd_find_command(argv[0]);
    /* 内建命令由会话层以完整 argv（含命令名）调用，此处不重复分派 */
    if (def == NULL || rs_builtin_is(def)) {
        return RS_ARGS_ERR_UNKNOWN;
    }
    return rs_cmd_run_node(rs, def, argc - 1, &argv[1]);
}

/* ============================== 命令行分派 ============================== */

#define RS_CMD_ARGC_MAX     16U     /* 单行最多 token 数 */

void rs_error(int code, const char *fmt, ...)
{
    char msg[64];
    va_list ap;

    va_start(ap, fmt);
    (void)str_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    ros_printf("ERR %d: %s\n", code, msg);
}

ros_task_t *rs_session_task(const roshell_t *rs)
{
    return (rs != NULL) ? rs->task : NULL;
}

const char *rs_session_version(const roshell_t *rs)
{
    return (rs != NULL) ? rs->version : NULL;
}

/* 行首 token 切分：空白分隔，双引号成对时视为单 token（保留引号内容） */
static int rs_tokenize(char *buf, char **argv, int max)
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

/*
 * 脚本引擎的命令 hook（规格 §8 statement -> command）：
 * 只分派命令名行（内建命令表 -> cmd 组件），不进入脚本引擎的语句解释，
 * 避免与引擎递归。返回 1 = 已处理（*ret 为命令返回值）；0 = 不是命令。
 */
static int rs_cmd_hook(roshell_t *rs, const char *line, int *ret)
{
    char buf[RS_LINE_MAX];
    char *argv[RS_CMD_ARGC_MAX];
    const rs_cmd_t *cmd;
    bool bg = false;
    int argc;
    int rc;

    if (line == NULL || ret == NULL) {
        return 0;
    }
    (void)str_copy(buf, sizeof(buf), line);
    argc = rs_tokenize(buf, argv, RS_CMD_ARGC_MAX);
    if (argc <= 0) {
        return 0;
    }
    /* '-' 前缀：持续命令不等待结束（异步启动） */
    if (argv[0][0] == '-' && argv[0][1] != '\0') {
        bg = true;
        argv[0]++;
    }
    cmd = rs_cmd_find_command(argv[0]);
    if (cmd != NULL) {
        if (rs_builtin_is(cmd)) {
            rc = rs_cmd_execute(rs, cmd, argc, argv, NULL, cmd->user, !bg);
        } else {
            rc = rs_cmd_exec_node(rs, cmd, argc - 1, &argv[1], !bg);
        }
        if (bg && rc == RS_CMD_RUNNING) {
            rc = 0;                     /* 已后台启动 */
        }
        *ret = rc;
        return 1;
    }
    if (bg) {
        return 0;                       /* 不是命令：按脚本一元负号处理 */
    }
    rc = rs_builtin_dispatch(rs, argc, argv);
    if (rc != RS_ARGS_ERR_UNKNOWN) {
        *ret = rc;
        return 1;
    }
    return 0;
}

void rs_cmd_install(roshell_t *rs)
{
    if (rs != NULL) {
        rs_set_cmd_hook(rs, rs_cmd_hook);
    }
}

int rs_cmd_dispatch(roshell_t *rs, const char *line)
{
    char buf[RS_LINE_MAX];
    char *argv[RS_CMD_ARGC_MAX];
    rs_err_t err;
    const rs_cmd_t *cmd;
    bool bg = false;
    int argc;
    int rc;

    if (line == NULL) {
        return RS_ERR_PARAM;
    }
    (void)str_copy(buf, sizeof(buf), line);
    argc = rs_tokenize(buf, argv, RS_CMD_ARGC_MAX);
    if (argc <= 0) {
        return RS_OK;                       /* 空行 */
    }

    /* '-' 前缀：持续命令不等待结束（异步启动），继续执行下一条命令 */
    if (argv[0][0] == '-' && argv[0][1] != '\0') {
        bg = true;
        argv[0]++;
    }
    cmd = rs_cmd_find_command(argv[0]);
    if (cmd != NULL) {
        if (rs_builtin_is(cmd)) {
            rc = rs_cmd_execute(rs, cmd, argc, argv, NULL, cmd->user, !bg);
        } else {
            rc = rs_cmd_exec_node(rs, cmd, argc - 1, &argv[1], !bg);
            if (rc < 0) {
                rs_error(rc, "command '%s' failed", argv[0]);
            }
        }
        if (bg && rc == RS_CMD_RUNNING) {
            return RS_OK;                   /* 已后台启动 */
        }
        return rc;
    }
    if (bg) {
        /* 不是命令：按脚本表达式（一元负号）处理，落到下方脚本路径 */
    }

    /* 非命令：整行交给脚本引擎 */
    if (rs == NULL) {
        rs_error(RS_ERR_PARAM, "no roshell instance");
        return -RS_ERR_PARAM;
    }
    (void)memset(&err, 0, sizeof(err));
    rc = rs_exec_line(rs, line, &err);
    if (rc != RS_OK) {
        if (err.msg[0] != '\0') {
            ros_printf("ERR %d (line %u): %s\n", err.code,
                       (unsigned)err.line, err.msg);
        } else {
            ros_printf("ERR %d (line %u)\n", rc, (unsigned)err.line);
        }
    }
    return rc;
}
