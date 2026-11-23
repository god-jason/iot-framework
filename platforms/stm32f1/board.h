#ifndef BOARD_STM32F103CBT6_H
#define BOARD_STM32F103CBT6_H

/*
 * board.h - STM32F103CBT6 最小板级支持
 *
 * SystemInit() 由启动汇编在 main() 之前调用，负责时钟与 SysTick；
 * 板载 LED 由应用（apps/<name>/app_main.c）初始化。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 系统时钟频率（HSE 8MHz -> PLL x9 = 72MHz），CMSIS 变量 */
extern uint32_t SystemCoreClock;

/* 启动汇编调用：时钟 + 1ms SysTick */
void SystemInit(void);

/* 板级早期初始化（时钟等），由平台 main 在通用流程前调用 */
void board_init(void);

/* 固件入口参数（platforms/stm32/stm32_main.c 使用） */
#define BOARD_NAME              "f103cb"
#define BOARD_APP_VECTOR_BASE   0x08006000U     /* App 区基址 */
#define BOARD_BLINK_STACK_SIZE  512U            /* 20KB SRAM 紧张，取最小可用值 */
#define BOARD_APP_STACK_SIZE    1024U           /* app 任务含 sched/命令注册，需较深调用栈 */

/* 把 trace 输出注册到应用初始化的 UART1(PA9)（硬件初始化在 apps/f103cb/app_uart.c） */
void board_trace_start(uint32_t baud);

/* 注册板级 RTC 后端：优先 LSE 32.768kHz 硬件 RTC，失败退回 1Hz 软件计时 */
void board_rtc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_STM32F103CBT6_H */
