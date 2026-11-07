#include "iot_shell.h"
#include "iot_string.h"

#define IOT_SHELL_MAX_CMD    16
#define IOT_SHELL_LINE_MAX   128
#define IOT_SHELL_ARGC_MAX   16

typedef struct {
    const char *name;
    const char *description;
    iot_shell_command_fn callback;
} shell_cmd_t;

static shell_cmd_t s_cmds[IOT_SHELL_MAX_CMD];
static char s_line[IOT_SHELL_LINE_MAX];
static uint32_t s_line_len;

int iot_shell_init(void)
{
    int i;
    for (i = 0; i < IOT_SHELL_MAX_CMD; i++) {
        s_cmds[i].callback = NULL;
    }
    s_line_len = 0;
    return IOT_OK;
}

int iot_shell_register(const iot_shell_command_t *command)
{
    int i;
    if (command == NULL || command->name == NULL || command->callback == NULL) {
        return -1;
    }
    for (i = 0; i < IOT_SHELL_MAX_CMD; i++) {
        if (s_cmds[i].callback == NULL) {
            s_cmds[i].name = command->name;
            s_cmds[i].description = command->description;
            s_cmds[i].callback = command->callback;
            return IOT_OK;
        }
    }
    return -1;
}

int iot_shell_unregister(const char *name)
{
    int i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < IOT_SHELL_MAX_CMD; i++) {
        if (s_cmds[i].callback != NULL &&
            iot_string_compare(s_cmds[i].name, name) == 0) {
            s_cmds[i].name = NULL;
            s_cmds[i].callback = NULL;
            return IOT_OK;
        }
    }
    return -1;
}

int iot_shell_execute(const char *command)
{
    char buf[IOT_SHELL_LINE_MAX];
    char *argv[IOT_SHELL_ARGC_MAX];
    int argc = 0;
    char *p;
    int i;

    if (command == NULL) {
        return -1;
    }

    /* 拷贝到可变缓冲并切分 */
    iot_string_ncopy(buf, command, sizeof(buf));
    p = buf;
    while (*p != '\0' && argc < IOT_SHELL_ARGC_MAX) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }

    if (argc == 0) {
        return -1;
    }

    for (i = 0; i < IOT_SHELL_MAX_CMD; i++) {
        if (s_cmds[i].callback != NULL &&
            iot_string_compare(s_cmds[i].name, argv[0]) == 0) {
            return s_cmds[i].callback(argc, argv);
        }
    }
    return -1;
}

int iot_shell_input(const char *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL || len == 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            s_line[s_line_len] = '\0';
            s_line_len = 0;
            (void)iot_shell_execute(s_line);
        } else if (s_line_len < IOT_SHELL_LINE_MAX - 1) {
            s_line[s_line_len++] = c;
        } else {
            s_line_len = 0;   /* 溢出丢弃 */
        }
    }
    return IOT_OK;
}

int iot_shell_process(void)
{
    return IOT_OK;
}