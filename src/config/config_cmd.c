/*
 * config_cmd.c - 配置管理 Shell 命令（JSON 方式）
 *
 * 依赖 config.h 的注册表与按键读写接口：
 *   cfg [list]               列出注册项
 *   cfg get <name>           KV 记录 -> JSON（无记录时输出默认值）
 *   cfg set <name> <json>    JSON -> 结构体（缺失字段默认）并保存
 *   cfg del <name>           删除记录
 *
 * JSON 文本建议紧凑（无空格）；如带空格，可用双引号包裹整段文本。
 */

#include "config_cmd.h"

#include "config.h"
#include "ros.h"
#include "ros_mem.h"
#include "roshell.h"
#include "strings.h"
#include "var_json.h"

#include <string.h>

/* JSON 解析用的字符串 arena 与命令行文本缓冲 */
#define CFG_CMD_STRBUF   128U
#define CFG_CMD_TEXTMAX  160U

/* 去掉 tokenizer 保留的外层双引号，并还原 \" 转义 */
static void cfg_unquote(const char *in, char *out, uint32_t cap)
{
    uint32_t n = 0U;

    if (in == NULL || cap == 0U) {
        return;
    }
    if (in[0] != '"') {
        (void)str_copy(out, cap, in);
        return;
    }
    in++;
    while (*in != '\0' && *in != '"' && (n + 1U) < cap) {
        if (in[0] == '\\' && in[1] == '"') {
            out[n++] = '"';
            in += 2;
            continue;
        }
        out[n++] = *in++;
    }
    out[n] = '\0';
}

/* 打印已注册配置项及其记录状态 */
static int cfg_list(void)
{
    uint32_t i;
    uint32_t n = config_item_count();

    if (n == 0U) {
        ros_puts("cfg: no registered config");
        return 0;
    }
    ros_puts("NAME                 ID          SIZE  STATE");
    for (i = 0U; i < n; i++) {
        const config_item_t *item = config_item_at(i);
        const char *state = "io!";
        void *buf;

        buf = ros_mem_malloc(item->size);
        if (buf != NULL) {
            int rc = config_key_load(item->id, buf, item->size);

            if (rc == CONFIG_OK) {
                state = "saved";
            } else if (rc == CONFIG_NONE) {
                state = "default";
            } else if (rc == CONFIG_ERR_SIZE) {
                state = "size!";
            }
            ros_mem_free(buf);
        }
        ros_printf("%-20s 0x%08X %5u  %s\n", item->name,
                   (unsigned)item->id, (unsigned)item->size, state);
    }
    return 0;
}

/* 读取配置并输出 JSON；无记录/长度不符时输出默认值 */
static int cfg_get(const config_item_t *item)
{
    void *data = ros_mem_malloc(item->size);
    char sbuf[CFG_CMD_STRBUF];
    char *json;

    if (data == NULL) {
        ros_puts("cfg: no memory");
        return -1;
    }
    if (config_key_load(item->id, data, item->size) != CONFIG_OK) {
        memset(data, 0, item->size);
        (void)item->from_json(data, "{}", sbuf, sizeof(sbuf));
    }
    json = item->to_json(data);
    ros_mem_free(data);
    if (json == NULL) {
        ros_puts("cfg: to json failed");
        return -1;
    }
    /* ros_printf 单次输出上限 192B，JSON 较长时用 puts 整体输出 */
    ros_printf("%s = ", item->name);
    ros_puts(json);
    var_free_json(json);
    return 0;
}

/* JSON -> 结构体 -> KV；缺失字段用默认值 */
static int cfg_set(const config_item_t *item, const char *text)
{
    void *data = ros_mem_malloc(item->size);
    char sbuf[CFG_CMD_STRBUF];
    int rc;
    char *json;

    if (data == NULL) {
        ros_puts("cfg: no memory");
        return -1;
    }
    memset(data, 0, item->size);
    if (!item->from_json(data, text, sbuf, sizeof(sbuf))) {
        ros_printf("cfg: %s: invalid json\n", item->name);
        ros_mem_free(data);
        return -1;
    }
    rc = config_key_save(item->id, data, item->size);
    if (rc != CONFIG_OK) {
        ros_printf("cfg: %s: save failed (%d)\n", item->name, rc);
        ros_mem_free(data);
        return -1;
    }
    json = item->to_json(data);
    ros_mem_free(data);
    if (json != NULL) {
        ros_printf("cfg: %s saved = ", item->name);
        ros_puts(json);
        var_free_json(json);
    } else {
        ros_printf("cfg: %s saved\n", item->name);
    }
    return 0;
}

static int cfg_del(const config_item_t *item)
{
    int rc = config_key_delete(item->id);

    if (rc != CONFIG_OK) {
        ros_printf("cfg: %s: delete failed (%d)\n", item->name, rc);
        return -1;
    }
    ros_printf("cfg: %s deleted\n", item->name);
    return 0;
}

static int cmd_cfg(roshell_t *rs, int argc, char **argv,
                   const rs_args_parsed_t *parsed, void *user)
{
    const char *action = NULL;
    const char *name = NULL;
    const char *text = NULL;
    const config_item_t *item;
    char jbuf[CFG_CMD_TEXTMAX];

    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;

    (void)rs_args_get_str(parsed, 0U, &action);
    if (action == NULL || action[0] == '\0' || str_icmp(action, "list")) {
        return cfg_list();
    }

    (void)rs_args_get_str(parsed, 1U, &name);
    if (name == NULL || name[0] == '\0') {
        ros_puts("cfg: missing config name");
        return -1;
    }
    item = config_find(name);
    if (item == NULL) {
        ros_printf("cfg: '%s' not registered\n", name);
        return -1;
    }

    if (str_icmp(action, "get")) {
        return cfg_get(item);
    }
    if (str_icmp(action, "del")) {
        return cfg_del(item);
    }
    if (str_icmp(action, "set")) {
        (void)rs_args_get_str(parsed, 2U, &text);
        if (text == NULL || text[0] == '\0') {
            ros_puts("cfg: missing json text");
            return -1;
        }
        cfg_unquote(text, jbuf, sizeof(jbuf));
        return cfg_set(item, jbuf);
    }

    ros_printf("cfg: unknown action '%s'\n", action);
    return -1;
}

static const rs_args_t s_cfg_args[] = {
    { "action", 0, RS_ARGS_TYPE_STR, true, false, "list",
      "list | get <name> | set <name> <json> | del <name>" },
    { "name", 0, RS_ARGS_TYPE_STR, true, false, NULL, "config name" },
    { "json", 0, RS_ARGS_TYPE_STR, true, false, NULL, "json text (set)" }
};

static const rs_cmd_t s_cfg_def = {
    .name = "cfg",
    .desc = "config json: list/get/set/del",
    .args = s_cfg_args,
    .arg_count = 3U,
    .on_start = cmd_cfg
};

int config_cmd_register(void)
{
    static bool ready;

    if (ready) {
        return 0;
    }
    if (rs_cmd_register(&s_cfg_def) != 0) {
        return -1;
    }
    ready = true;
    return 0;
}
