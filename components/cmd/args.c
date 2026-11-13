/*
 * args.c - 命令参数解析实现
 *
 * 解析规则：
 *   --name value / --name=value / -x value；
 *   --flag / --flag=on / -f 为布尔开关；
 *   其余 token 依次绑定到 positional=true 的参数；
 *   "--" 之后全部按位置参数处理。
 */

#include "args.h"
#include "ros.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>

/* ---------------- 小工具 ---------------- */

static bool args_value_valid(uint8_t type, const char *s)
{
    int32_t iv;
    uint32_t uv;
    bool b;

    if (s == NULL || s[0] == '\0') {
        return false;
    }
    switch (type) {
    case ARGS_TYPE_INT:
        return str_parse_int(s, &iv);
    case ARGS_TYPE_UINT:
        return str_parse_uint(s, &uv);
    case ARGS_TYPE_BOOL:
        return str_parse_bool(s, &b);
    case ARGS_TYPE_STR:
    default:
        return true;
    }
}

/* 按长名查找（仅选项） */
static int args_find_len(const args_def_t *args, uint8_t count,
                         const char *name, uint32_t len)
{
    uint8_t i;

    for (i = 0U; i < count; i++) {
        const args_def_t *a = &args[i];

        if (a->positional || a->name == NULL) {
            continue;
        }
        if (strlen(a->name) == len && strncmp(a->name, name, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* 第 nth 个位置参数在定义表中的下标 */
static int args_positional_index(const args_def_t *args, uint8_t count,
                                 uint8_t nth)
{
    uint8_t i;
    uint8_t k = 0U;

    for (i = 0U; i < count; i++) {
        if (args[i].positional) {
            if (k == nth) {
                return (int)i;
            }
            k++;
        }
    }
    return -1;
}

static int args_fail(args_parsed_t *parsed, int err, const char *arg)
{
    parsed->error = err;
    (void)snprintf(parsed->err_arg, sizeof(parsed->err_arg), "%s",
                   (arg != NULL) ? arg : "?");
    return err;
}

/* ---------------- 查找 ---------------- */

int args_find(const args_def_t *args, uint8_t arg_count, const char *name)
{
    if (args == NULL || name == NULL) {
        return -1;
    }
    return args_find_len(args, arg_count, name, (uint32_t)strlen(name));
}

int args_find_short(const args_def_t *args, uint8_t arg_count,
                   char short_name)
{
    uint8_t i;

    if (args == NULL || short_name == '\0') {
        return -1;
    }
    for (i = 0U; i < arg_count; i++) {
        const args_def_t *a = &args[i];

        if (!a->positional && a->short_name == short_name) {
            return (int)i;
        }
    }
    return -1;
}

/* ---------------- 解析 ---------------- */

int args_parse(const args_def_t *args, uint8_t arg_count,
              char *const *argv, int argc, args_parsed_t *parsed)
{
    bool options_done = false;
    int t;

    if (args == NULL || parsed == NULL || (argv == NULL && argc != 0) ||
        arg_count > ARGS_MAX) {
        return ARGS_ERR_PARAM;
    }
    memset(parsed, 0, sizeof(*parsed));
    parsed->args = args;
    parsed->arg_count = arg_count;
    parsed->error = ARGS_OK;

    for (t = 0; t < argc; t++) {
        const char *tok = argv[t];
        int idx;

        if (tok == NULL) {
            return args_fail(parsed, ARGS_ERR_PARAM, NULL);
        }

        if (!options_done) {
            if (strcmp(tok, "--") == 0) {
                options_done = true;
                continue;
            }
            if (tok[0] == '-' && tok[1] == '-' && tok[2] != '\0') {
                const char *nm = tok + 2;
                const char *eq = strchr(nm, '=');
                uint32_t nlen = (eq != NULL) ? (uint32_t)(eq - nm)
                                             : (uint32_t)strlen(nm);
                const char *val = (eq != NULL) ? (eq + 1) : NULL;

                idx = args_find_len(args, arg_count, nm, nlen);
                if (idx < 0) {
                    return args_fail(parsed, ARGS_ERR_UNKNOWN, "long option");
                }
                if (args[idx].type == ARGS_TYPE_BOOL) {
                    if (val != NULL) {
                        bool b;

                        if (!str_parse_bool(val, &b)) {
                            return args_fail(parsed, ARGS_ERR_TYPE,
                                             args[idx].name);
                        }
                    }
                    parsed->present[idx] = true;
                    parsed->vals[idx] = val;    /* NULL 表示开关打开 */
                } else {
                    if (val == NULL) {
                        if (t + 1 >= argc) {
                            return args_fail(parsed, ARGS_ERR_MISSING_VALUE,
                                             args[idx].name);
                        }
                        val = argv[++t];
                    }
                    if (!args_value_valid(args[idx].type, val)) {
                        return args_fail(parsed, ARGS_ERR_TYPE, args[idx].name);
                    }
                    parsed->present[idx] = true;
                    parsed->vals[idx] = val;
                }
                continue;
            }
            if (tok[0] == '-' && tok[1] != '\0') {
                char c = tok[1];

                if (tok[2] != '\0') {
                    return args_fail(parsed, ARGS_ERR_UNKNOWN, tok);
                }
                idx = args_find_short(args, arg_count, c);
                if (idx < 0) {
                    return args_fail(parsed, ARGS_ERR_UNKNOWN, tok);
                }
                if (args[idx].type == ARGS_TYPE_BOOL) {
                    parsed->present[idx] = true;
                    parsed->vals[idx] = NULL;
                } else {
                    if (t + 1 >= argc) {
                        return args_fail(parsed, ARGS_ERR_MISSING_VALUE,
                                         args[idx].name);
                    }
                    if (!args_value_valid(args[idx].type, argv[t + 1])) {
                        return args_fail(parsed, ARGS_ERR_TYPE, args[idx].name);
                    }
                    parsed->present[idx] = true;
                    parsed->vals[idx] = argv[++t];
                }
                continue;
            }
        }

        /* 位置参数 */
        idx = args_positional_index(args, arg_count, parsed->positionals);
        if (idx < 0) {
            return args_fail(parsed, ARGS_ERR_EXTRA, tok);
        }
        if (!args_value_valid(args[idx].type, tok)) {
            return args_fail(parsed, ARGS_ERR_TYPE, args[idx].name);
        }
        parsed->present[idx] = true;
        parsed->vals[idx] = tok;
        parsed->positionals++;
    }

    /* 必填检查 */
    {
        uint8_t i;

        for (i = 0U; i < arg_count; i++) {
            if (args[i].required && !parsed->present[i]) {
                return args_fail(parsed, ARGS_ERR_MISSING_ARG, args[i].name);
            }
        }
    }
    return ARGS_OK;
}

/* ---------------- 取值 ---------------- */

static const char *args_value(const args_parsed_t *parsed, uint8_t index)
{
    if (parsed == NULL || parsed->args == NULL ||
        index >= parsed->arg_count) {
        return NULL;
    }
    if (parsed->present[index]) {
        return parsed->vals[index];
    }
    return parsed->args[index].def;
}

bool args_get_str(const args_parsed_t *parsed, uint8_t index, const char **out)
{
    const char *v;

    if (out == NULL || parsed == NULL || parsed->args == NULL ||
        index >= parsed->arg_count) {
        return false;
    }
    v = args_value(parsed, index);
    *out = v;
    return v != NULL;
}

bool args_get_int(const args_parsed_t *parsed, uint8_t index, int *out)
{
    const char *v;
    char *end = NULL;
    long n;

    if (out == NULL || parsed == NULL || parsed->args == NULL ||
        index >= parsed->arg_count) {
        return false;
    }
    v = args_value(parsed, index);
    if (v == NULL) {
        return false;
    }
    n = strtol(v, &end, 10);
    if (end == v || *end != '\0') {
        return false;
    }
    *out = (int)n;
    return true;
}

bool args_get_uint(const args_parsed_t *parsed, uint8_t index, uint32_t *out)
{
    const char *v;
    char *end = NULL;
    unsigned long n;

    if (out == NULL || parsed == NULL || parsed->args == NULL ||
        index >= parsed->arg_count) {
        return false;
    }
    v = args_value(parsed, index);
    if (v == NULL || v[0] == '-') {
        return false;
    }
    n = strtoul(v, &end, 10);
    if (end == v || *end != '\0') {
        return false;
    }
    *out = (uint32_t)n;
    return true;
}

bool args_get_bool(const args_parsed_t *parsed, uint8_t index, bool *out)
{
    const char *v;

    if (out == NULL || parsed == NULL || parsed->args == NULL ||
        index >= parsed->arg_count) {
        return false;
    }
    if (parsed->present[index]) {
        if (parsed->vals[index] == NULL) {
            *out = true;        /* 开关出现且未带值 */
            return true;
        }
        return str_parse_bool(parsed->vals[index], out);
    }
    v = parsed->args[index].def;
    if (v == NULL) {
        *out = false;           /* 未出现且无默认值 */
        return true;
    }
    return str_parse_bool(v, out);
}

const char *args_error_str(int err)
{
    switch (err) {
    case ARGS_OK:                return "ok";
    case ARGS_ERR_PARAM:         return "invalid parameter";
    case ARGS_ERR_UNKNOWN:       return "unknown option";
    case ARGS_ERR_MISSING_VALUE: return "missing option value";
    case ARGS_ERR_MISSING_ARG:   return "missing required argument";
    case ARGS_ERR_TYPE:          return "invalid value type";
    case ARGS_ERR_EXTRA:         return "unexpected argument";
    default:                    return "command error";
    }
}
