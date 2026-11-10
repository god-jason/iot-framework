#ifndef BOARD_STM32F407_H
#define BOARD_STM32F407_H

/*
 * board.h - STM32F407VET6 通用核心板板级支持
 *
 * 时钟由 Cube 的 system_stm32f4xx.c + 本文件的 SystemClock_Config() 完成；
 * 板载 LED 接 PA6 / PA7（低电平点亮）；
 * USART1（PA9 发送 / PA10 接收）同时用于 trace、Shell 与 printf。
 */

#include <stdint.h>

#include "hal_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 系统时钟频率（168MHz），CMSIS 变量 */
extern uint32_t SystemCoreClock;

/* 168MHz 时钟树：HSE 8MHz -> PLL -> SYSCLK 168MHz / APB1 42MHz / APB2 84MHz */
void SystemClock_Config(void);

/* 板载 LED（PA6 / PA7） */
void board_led_init(void);
void board_led_toggle(void);

/* 初始化 USART1（收发）并返回框架 UART 句柄 */
void board_uart_init(uint32_t baudrate);
hal_uart_t *board_uart1(void);

/* 初始化 USART1 并把 trace 输出注册到该串口 */
void board_trace_start(uint32_t baudrate);

/* 平台层钩子：Shell 使用同一个 UART1 句柄（platforms/stm32/stm32_shell.c 弱引用） */
hal_uart_t *board_shell_uart(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_STM32F407_H */
