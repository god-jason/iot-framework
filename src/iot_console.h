#ifndef IOT_CONSOLE_H
#define IOT_CONSOLE_H

/*
 * 主串口（UART1）控制台（含命令行 Shell）：
 *  - 默认输出日志（注册 logger 后端 -> UART）
 *  - 接受命令输入（UART 接收 -> shell 分发）
 *  - 命令注册/执行（按空格切分 argv）
 */

#include "iot_types.h"
#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    port_uart_handle_t *uart;
    uint8_t *rx_buffer;
    uint32_t rx_buffer_size;
} iot_console_config_t;

int iot_console_init(const iot_console_config_t *config);
int iot_console_start(void);

/* 主循环调用：泵取 UART 接收字节并喂给 shell */
int iot_console_process(void);

/* 直接输出一行（绕过 logger，用于控制台回显/提示） */
int iot_console_puts(const char *s);

/* ---------------- Shell ---------------- */
typedef int (*iot_shell_command_fn)(int argc, char **argv);

typedef struct {
    const char *name;
    const char *description;
    iot_shell_command_fn callback;
} iot_shell_command_t;

int iot_shell_init(void);

int iot_shell_register(const iot_shell_command_t *command);
int iot_shell_unregister(const char *name);

/* 喂入字节流（行以 \n 结束即执行） */
int iot_shell_input(const char *data, uint32_t len);
int iot_shell_process(void);

/* 直接执行一行命令 */
int iot_shell_execute(const char *command);

#ifdef __cplusplus
}
#endif

#endif /* IOT_CONSOLE_H */