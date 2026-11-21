/*
 * roshell_cmd.c - roshell 全局命令表
 *
 * 命令 usage / help 文本生成、全局命令表注册 / 查找 / 运行、
 * 运行中命令登记与生命周期通知钩子（on_pause / on_resume / on_kill）、
 * argv 分派入口。参数解析在 roshell_args.c，会话与行编辑在
 * roshell_context.c。
 */

#include "roshell.h"
#include "roshell_cmd_internal.h"
#include "ros.h"
#include "common.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ============ 命令表（原 cmd 组件） ============ */
/*
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

int rs_cmd_run(const rs_cmd_t *table, uint32_t count,
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
            if (table[i].fn != NULL) {
                rs_cmd_ctx_t ctx;

                ctx.rs = NULL;
                ctx.argc = argc;
                ctx.argv = argv;
                ctx.parsed = pparsed;
                ctx.user = (table[i].user != NULL) ? table[i].user : user;
                return table[i].fn(&ctx);
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
    if (def == NULL || def->name == NULL || def->fn == NULL) {
        return false;
    }
    if (map_contains(&rs_cmd_table, def->name)) {
        ros_printf("cmd dup: %s\r\n", def->name);
        return false;
    }
    if (!map_put(&rs_cmd_table, def->name, (void *)def)) {
        ros_mem_info_t info;

        (void)memset(&info, 0, sizeof(info));
        (void)ros_mem_get_info(&info);
        ros_printf("cmd put fail: %s free=%u max=%u\r\n", def->name,
                   (unsigned)info.free_bytes, (unsigned)info.max_free_bytes);
        return false;
    }
    return true;
}

/* 把内建命令全部登记到命令表（调用方持有临界区） */
static void rs_cmd_add_builtins(void)
{
    const rs_cmd_t *builtins;
    uint32_t count = 0U;
    uint32_t i;

    builtins = rs_builtin_table(&count);
    for (i = 0U; i < count; i++) {
        if (!rs_cmd_add(&builtins[i])) {
            ros_printf("cmd add fail: %s\r\n",
                       builtins[i].name != NULL ? builtins[i].name : "?");
        }
    }
}

static void rs_cmd_commands_ensure(void)
{
    ros_port_critical_enter();
    if (!s_commands_ready) {
        if (map_init(&rs_cmd_table) == 0) {
            ros_mem_info_t info;

            s_commands_ready = true;
            rs_cmd_add_builtins();
            (void)memset(&info, 0, sizeof(info));
            (void)ros_mem_get_info(&info);
            ros_printf("cmd table: %u entries, free=%u\r\n",
                       (unsigned)map_size(&rs_cmd_table),
                       (unsigned)info.free_bytes);
        }
    }
    ros_port_critical_exit();
}

int rs_cmd_register(const rs_cmd_t *def)
{
    bool ok;

    if (def == NULL || def->name == NULL || def->fn == NULL) {
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

/* 记录“某任务正在执行哪条命令”，供 pause/resume/kill 查找钩子 */
typedef struct {
    ros_task_t *task;
    const rs_cmd_t *def;
} rs_cmd_active_t;

static vec_t s_active;
static bool s_active_ready;

void rs_cmd_active_push(ros_task_t *task, const rs_cmd_t *def)
{
    rs_cmd_active_t item;

    if (task == NULL) {
        return;
    }
    ros_port_critical_enter();
    if (!s_active_ready) {
        if (vec_init(&s_active, sizeof(rs_cmd_active_t), 4U) != 0) {
            ros_port_critical_exit();
            return;
        }
        s_active_ready = true;
    }
    item.task = task;
    item.def = def;
    (void)vec_push(&s_active, &item);
    ros_port_critical_exit();
}

void rs_cmd_active_pop(ros_task_t *task, const rs_cmd_t *def)
{
    uint32_t i;

    ros_port_critical_enter();
    i = vec_size(&s_active);
    while (i > 0U) {
        const rs_cmd_active_t *item =
            (const rs_cmd_active_t *)vec_at(&s_active, i - 1U);

        if (item->task == task && item->def == def) {
            (void)vec_remove(&s_active, i - 1U, NULL);
            ros_port_critical_exit();
            return;
        }
        i--;
    }
    ros_port_critical_exit();
}

static const rs_cmd_t *rs_cmd_active_find(const ros_task_t *task)
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

int rs_cmd_notify_pause(const ros_task_t *task)
{
    const rs_cmd_t *def = rs_cmd_active_find(task);

    if (def == NULL || def->on_pause == NULL) {
        return 0;
    }
    return def->on_pause(def->user, (ros_task_t *)task);
}

int rs_cmd_notify_resume(const ros_task_t *task)
{
    const rs_cmd_t *def = rs_cmd_active_find(task);

    if (def == NULL || def->on_resume == NULL) {
        return 0;
    }
    return def->on_resume(def->user, (ros_task_t *)task);
}

int rs_cmd_notify_kill(const ros_task_t *task)
{
    const rs_cmd_t *def = rs_cmd_active_find(task);
    int rc = 0;

    if (def != NULL && def->on_kill != NULL) {
        rc = def->on_kill(def->user, (ros_task_t *)task);
    }
    /* 任务即将被停止/销毁：清理登记，避免悬空 */
    rs_cmd_active_remove_task(task);
    return rc;
}

int rs_cmd_run_node(const rs_cmd_t *def, int argc, char **argv)
{
    rs_args_parsed_t parsed;
    const rs_args_parsed_t *pparsed = NULL;
    ros_task_t *self = ros_current_task;
    int rc;

    if (def == NULL || def->fn == NULL ||
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
    {
        rs_cmd_ctx_t ctx;

        ctx.rs = NULL;
        ctx.argc = argc;
        ctx.argv = argv;
        ctx.parsed = pparsed;
        ctx.user = def->user;
        rs_cmd_active_push(self, def);
        rc = def->fn(&ctx);
        rs_cmd_active_pop(self, def);
    }
    return rc;
}

int rs_builtin_dispatch(int argc, char **argv)
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
    return rs_cmd_run_node(def, argc - 1, &argv[1]);
}
