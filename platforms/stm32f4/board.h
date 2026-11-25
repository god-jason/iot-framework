#ifndef BOARD_STM32F407_H
#define BOARD_STM32F407_H

/*
 * board.h - STM32F407VET6 通用核心板板级支持
 *
 * 时钟由 Cube 的 system_stm32f4xx.c + 本文件的 SystemClock_Config() 完成；
 * USART1（PA9 发送 / PA10 接收）同时用于 trace、Shell 与 printf。
 * 板载 LED 由应用（apps/<name>/app_main.c）初始化。
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

/* 板级早期初始化（HAL + 时钟树），由平台 main 在通用流程前调用 */
void board_init(void);

/* 固件入口参数（platforms/stm32/stm32_main.c 使用） */
#define BOARD_NAME              "stm32f4xx"
#define BOARD_APP_VECTOR_BASE   0x08010000U     /* APP 区基址（192KB） */
#define BOARD_BLINK_STACK_SIZE  1024U
#define BOARD_APP_STACK_SIZE    2048U

/* 初始化 USART1（收发）并返回框架 UART 句柄 */
void board_uart_init(uint32_t baudrate);
hal_uart_t *board_uart1(void);

/* USART2（PA2 TX / PA3 RX）：ML307 4G 模组 AT 通道，首次调用时初始化（115200）。
 * 注意：PA2/PA3 与 PWM 编号 2/3（TIM2_CH3/CH4）冲突，接模组时这两路 PWM 不可用。 */
hal_uart_t *board_uart2(void);

/* 初始化 USART1 并把 trace 输出注册到该串口 */
void board_trace_start(uint32_t baudrate);

/* 平台层钩子：Shell 使用同一个 UART1 句柄（platforms/stm32/stm32_shell.c 弱引用） */
hal_uart_t *board_shell_uart(void);

/* 注册板级 RTC 后端（1Hz 软件计时，默认 ROS_RTC_DEFAULT_UNIX） */
void board_rtc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_STM32F407_H */
