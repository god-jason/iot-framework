#ifndef APP_UART_H
#define APP_UART_H

/*
 * app_uart.h - F103CB 应用串口初始化
 *
 * UART1(PA9/PA10)：日志（stdout/trace）与 roshell；
 * UART2(PA2/PA3) ：ML307 4G 模组 AT 通道。
 */

#include "hal_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 UART1 / UART2（115200 8N1），并注册 trace 输出 */
void app_uart_init(void);

/* 日志/shell 使用的 UART1 句柄 */
hal_uart_t *app_uart_console(void);

/* ML307 使用的 UART2 句柄 */
hal_uart_t *app_uart_ml307(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UART_H */
