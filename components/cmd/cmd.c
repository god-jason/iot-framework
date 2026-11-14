/*
 * cmd.c - 命令用法/帮助文本生成与命令表分发
 */

#include "cmd.h"
#include "ros.h"
#include "common.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int cmd_append(char *buf, uint32_t size, int off, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (off < 0) {
        return -1;
    }
    va_start(ap, fmt);
    n = vsnprintf(&buf[off], size - (uint32_t)off, fmt, ap);
    va_end(ap);
    if (n < 0 || (uint32_t)n >= size - (uint32_t)off) {
        return -1;
    }
    return off + n;
}

static const char *cmd_type_placeholder(uint8_t type)
{
    switch (type) {
    case ARGS_TYPE_INT:
    case ARGS_TYPE_UINT:
        return "<n>";
    default:
        return "<s>";
    }
}

int cmd_usage(const cmd_def_t *def, char *buf, uint32_t size)
{
    int off = 0;
    uint8_t i;

    if (def == NULL || buf == NULL || size == 0U) {
        return -1;
    }
    off = cmd_append(buf, size, off, "%s", def->name);
    for (i = 0U; i < def->arg_count; i++) {
        const args_def_t *a = &def->args[i];

        if (a->positional) {
            off = cmd_append(buf, size, off, a->required ? " <%s>" : " [%s]",
                             (a->name != NULL) ? a->name : "arg");
        } else if (a->type == ARGS_TYPE_BOOL) {
            off = cmd_append(buf, size, off, a->required ? " --%s" : " [--%s]",
                             a->name);
        } else {
            off = cmd_append(buf, size, off,
                             a->required ? " --%s %s" : " [--%s %s]",
                             a->name, cmd_type_placeholder(a->type));
        }
    }
    return off;
}

int cmd_help(const cmd_def_t *def, char *buf, uint32_t size)
{
    int off = 0;
    uint8_t i;

    if (def == NULL || buf == NULL || size == 0U) {
        return -1;
    }
    off = cmd_append(buf, size, off, "%s - %s\n", def->name,
                     (def->desc != NULL) ? def->desc : "");
    off = cmd_append(buf, size, off, "usage: ");
    {
        int n = cmd_usage(def, &buf[off], size - (uint32_t)off);

        if (n < 0) {
            return -1;
        }
        off += n;
    }
    off = cmd_append(buf, size, off, "\n");
    off = cmd_append(buf, size, off, "arguments:\n");
    for (i = 0U; i < def->arg_count; i++) {
        const args_def_t *a = &def->args[i];

        if (a->positional) {
            off = cmd_append(buf, size, off, "  %-14s %s\n", a->name,
                             (a->desc != NULL) ? a->desc : "");
        } else if (a->short_name != '\0') {
            off = cmd_append(buf, size, off, "  --%s, -%c  %s\n", a->name,
                             a->short_name, (a->desc != NULL) ? a->desc : "");
        } else {
            off = cmd_append(buf, size, off, "  --%s  %s\n", a->name,
                             (a->desc != NULL) ? a->desc : "");
        }
        if (a->def != NULL) {
            off = cmd_append(buf, size, off, "                 default: %s\n",
                             a->def);
        }
    }
    return off;
}

int cmd_run(const cmd_entry_t *table, uint32_t count,
            int argc, char **argv, void *user)
{
    args_parsed_t parsed;
    uint32_t i;
    int rc;

    if (table == NULL || argc < 1 || argv == NULL || argv[0] == NULL) {
        return ARGS_ERR_PARAM;
    }
    for (i = 0U; i < count; i++) {
        if (table[i].def != NULL && table[i].def->name != NULL &&
            strcmp(table[i].def->name, argv[0]) == 0) {
            rc = args_parse(table[i].def->args, table[i].def->arg_count,
                           &argv[1], argc - 1, &parsed);
            if (rc != ARGS_OK) {
                return rc;
            }
            if (table[i].fn != NULL) {
                return table[i].fn(&parsed, user);
            }
            return ARGS_OK;
        }
    }
    return ARGS_ERR_UNKNOWN;
}

/* ============================== 全局命令表 ============================== */

vec_t commands;

static bool s_commands_ready;

static void cmd_commands_ensure(void)
{
    if (!s_commands_ready) {
        if (vec_init(&commands, sizeof(cmd_node_t), 8U) == 0) {
            s_commands_ready = true;
        }
    }
}

int cmd_register(const cmd_def_t *def, cmd_fn_t fn, void *user)
{
    cmd_node_t node;

    if (def == NULL || def->name == NULL || fn == NULL) {
        return -1;
    }
    cmd_commands_ensure();
    if (!s_commands_ready) {
        return -1;                  /* 命令表初始化失败 */
    }
    if (cmd_find_command(def->name) != NULL) {
        return -1;
    }
    node.def = def;
    node.fn = fn;
    node.user = user;
    return vec_push(&commands, &node);
}

void cmd_unregister_all(void)
{
    cmd_commands_ensure();
    if (s_commands_ready) {
        vec_clear(&commands);
    }
}

const cmd_node_t *cmd_find_command(const char *name)
{
    uint32_t i;

    if (name == NULL) {
        return NULL;
    }
    cmd_commands_ensure();
    for (i = 0; i < vec_size(&commands); i++) {
        const cmd_node_t *node = (const cmd_node_t *)vec_at(&commands, i);

        if (node->def != NULL && node->def->name != NULL &&
            str_icmp(node->def->name, name)) {
            return node;
        }
    }
    return NULL;
}

const cmd_node_t *cmd_first(void)
{
    cmd_commands_ensure();
    return (const cmd_node_t *)vec_at(&commands, 0);
}

const cmd_node_t *cmd_next(const cmd_node_t *node)
{
    uint32_t i;

    if (node == NULL) {
        return NULL;
    }
    cmd_commands_ensure();
    for (i = 0; i < vec_size(&commands); i++) {
        if ((const cmd_node_t *)vec_at(&commands, i) == node) {
            return (i + 1U < vec_size(&commands))
                       ? (const cmd_node_t *)vec_at(&commands, i + 1U)
                       : NULL;
        }
    }
    return NULL;
}

int cmd_run_node(const cmd_node_t *node, int argc, char **argv)
{
    args_parsed_t parsed;
    int rc;

    if (node == NULL || node->def == NULL || node->fn == NULL ||
        argc < 0 || (argv == NULL && argc != 0)) {
        return ARGS_ERR_PARAM;
    }
    rc = args_parse(node->def->args, node->def->arg_count, argv, argc, &parsed);
    if (rc != ARGS_OK) {
        return rc;
    }
    return node->fn(&parsed, node->user);
}

int cmd_dispatch(int argc, char **argv)
{
    const cmd_node_t *node;

    if (argc < 1 || argv == NULL || argv[0] == NULL) {
        return ARGS_ERR_PARAM;
    }
    node = cmd_find_command(argv[0]);
    if (node == NULL) {
        return ARGS_ERR_UNKNOWN;
    }
    return cmd_run_node(node, argc - 1, &argv[1]);
}
