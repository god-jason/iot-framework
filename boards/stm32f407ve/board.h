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

/* 注册 12 路 PWM（编号 0~11，按注册顺序）：
 *   0~3   TIM2_CH1~4 (PA0~PA3)，默认 1kHz
 *   4~7   TIM4_CH1~4 (PB6~PB9)，默认 2kHz
 *   8~9   TIM3_CH3~4 (PB0/PB1)，默认 3kHz（PA6/PA7 为 LED，避开）
 *   10~11 TIM12_CH1~2 (PB14/PB15)，默认 4kHz */
void board_pwm_register(void);

/* 注册板级 RTC 后端（1Hz 软件计时，默认 ROS_RTC_DEFAULT_UNIX） */
void board_rtc_init(void);

/* 风扇：复用 PWM 编号 0（TIM2_CH1 / PA0）输出，25kHz */
void board_fan_init(void);

/* 步进电机：STEP=PB12，DIR=PB13 */
void board_stepper_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_STM32F407_H */
