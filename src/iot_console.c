#include "iot_console.h"
#include "iot_logger.h"

#include <stdio.h>
#include <string.h>

static port_uart_handle_t *s_uart = NULL;
static uint8_t *s_rx = NULL;
static uint32_t s_rx_size = 0;

/* logger 后端：输出到 UART（简短英文格式 [L][tag] msg） */
static void console_log_output(iot_log_level_t level, const char *tag,
                               const char *message, void *user)
{
    static const char LEVEL_CH[] = {'D', 'I', 'W', 'E', 'F'};
    char line[160];
    int n;
    (void)user;

    if (s_uart == NULL) {
        return;
    }
    n = snprintf(line, sizeof(line), "[%c][%s] %s\r\n",
                 LEVEL_CH[level], (tag != NULL) ? tag : "-", message);
    if (n > 0) {
        port_uart_transmit(s_uart, (const uint8_t *)line, (size_t)n, 100);
    }
}

int iot_console_init(const iot_console_config_t *config)
{
    iot_log_backend_t backend;

    if (config == NULL || config->uart == NULL) {
        return -1;
    }
    s_uart = config->uart;
    s_rx = config->rx_buffer;
    s_rx_size = config->rx_buffer_size;

    /* 注册日志后端：默认输出日志 */
    backend.level = IOT_LOG_DEBUG;
    backend.tag = NULL;
    backend.output = console_log_output;
    backend.user = NULL;
    iot_log_register(&backend);

    return iot_shell_init();
}

int iot_console_start(void)
{
    iot_console_puts("iot-os console ready");   /* console ready */
    return PORT_OK;
}

int iot_console_puts(const char *s)
{
    if (s_uart == NULL || s == NULL) {
        return -1;
    }
    /* 输出一行 + CRLF */
    port_uart_transmit(s_uart, (const uint8_t *)s, strlen(s), 100);
    port_uart_transmit(s_uart, (const uint8_t *)"\r\n", 2, 100);
    return PORT_OK;
}

int iot_console_process(void)
{
    uint32_t n;

    if (s_uart == NULL) {
        return -1;
    }

    n = (uint32_t)port_uart_get_rx_count(s_uart);
    while (n > 0 && s_rx != NULL && s_rx_size > 0) {
        uint32_t chunk = (n > s_rx_size) ? s_rx_size : n;
        port_status_t r = port_uart_receive(s_uart, s_rx, chunk, 100);
        if (r != PORT_OK) {
            break;
        }
        iot_shell_input((const char *)s_rx, chunk);
        n = (uint32_t)port_uart_get_rx_count(s_uart);
    }
    return PORT_OK;
}

/* ==================== Shell ==================== */
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
        if (s_cmds[i].callback != NULL && strcmp(s_cmds[i].name, name) == 0) {
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
    strncpy(buf, command, sizeof(buf) - 1u);
    buf[sizeof(buf) - 1u] = '\0';
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
        if (s_cmds[i].callback != NULL && strcmp(s_cmds[i].name, argv[0]) == 0) {
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