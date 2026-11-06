#ifndef IOT_CONSOLE_H
#define IOT_CONSOLE_H

/*
 * 主串口（UART1）控制台：
 *  - 默认输出日志（注册 logger 后端 -> UART）
 *  - 接受命令输入（UART 接收 -> shell 分发）
 */

#include "iot_types.h"
#include "iot_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const iot_port_uart_t *uart;
    uint8_t *rx_buffer;
    uint32_t rx_buffer_size;
} iot_console_config_t;

int iot_console_init(const iot_console_config_t *config);
int iot_console_start(void);

/* 主循环调用：泵取 UART 接收字节并喂给 shell */
int iot_console_process(void);

/* 直接输出一行（绕过 logger，用于控制台回显/提示） */
int iot_console_puts(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* IOT_CONSOLE_H */