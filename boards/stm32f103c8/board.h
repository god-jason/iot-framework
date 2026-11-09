#ifndef BOARD_STM32F103C8_H
#define BOARD_STM32F103C8_H

/*
 * board.h - STM32F103C8T6 最小板级支持
 *
 * SystemInit() 由启动汇编在 main() 之前调用，负责时钟与 SysTick；
 * 板载 LED 接在 PC13（低电平点亮）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 系统时钟频率（HSE 8MHz -> PLL x9 = 72MHz），CMSIS 变量 */
extern uint32_t SystemCoreClock;

/* 启动汇编调用：时钟 + 1ms SysTick */
void SystemInit(void);

/* 板载 LED（PC13） */
void board_led_init(void);
void board_led_toggle(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_STM32F103C8_H */
