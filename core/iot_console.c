#include "iot_console.h"
#include "iot_logger.h"
#include "iot_shell.h"

#include <stdio.h>
#include <string.h>

static const iot_port_uart_t *s_uart = NULL;
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

    if (s_uart == NULL || s_uart->send == NULL) {
        return;
    }
    n = snprintf(line, sizeof(line), "[%c][%s] %s\r\n",
                 LEVEL_CH[level], (tag != NULL) ? tag : "-", message);
    if (n > 0) {
        s_uart->send(s_uart->ctx, (const uint8_t *)line, (uint32_t)n);
    }
}

int iot_console_init(const iot_console_config_t *config)
{
    iot_log_backend_t backend;

    if (config == NULL || config->uart == NULL) {
        return IOT_ERR_PARAM;
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
    return IOT_OK;
}

int iot_console_puts(const char *s)
{
    if (s_uart == NULL || s_uart->send == NULL || s == NULL) {
        return IOT_ERR_STATE;
    }
    /* 输出一行 + CRLF */
    s_uart->send(s_uart->ctx, (const uint8_t *)s, (uint32_t)strlen(s));
    s_uart->send(s_uart->ctx, (const uint8_t *)"\r\n", 2);
    return IOT_OK;
}

int iot_console_process(void)
{
    uint32_t n;

    if (s_uart == NULL || s_uart->available == NULL || s_uart->receive == NULL) {
        return IOT_ERR_STATE;
    }

    n = s_uart->available(s_uart->ctx);
    while (n > 0 && s_rx != NULL && s_rx_size > 0) {
        uint32_t chunk = (n > s_rx_size) ? s_rx_size : n;
        int r = s_uart->receive(s_uart->ctx, s_rx, chunk);
        if (r <= 0) {
            break;
        }
        iot_shell_input((const char *)s_rx, (uint32_t)r);
        n = s_uart->available(s_uart->ctx);
    }
    return IOT_OK;
}