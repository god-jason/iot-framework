#ifndef BOARD_STM32F103CBT6_H
#define BOARD_STM32F103CBT6_H

/*
 * board.h - STM32F103CBT6 最小板级支持
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

/* 向 device 组件注册本板 PWM（只绑定句柄，首次使用时才初始化硬件）：
 *   编号 0~3   = TIM2_CH1~4 (PA0~PA3, 默认 1kHz)
 *   编号 4~7   = TIM3_CH1~4 (PA6/PA7/PB0/PB1, 默认 2kHz)
 *   编号 8~11  = TIM4_CH1~4 (PB6/PB7/PB8/PB9, 默认 3kHz) */
void board_pwm_register(void);

/* 初始化 USART1(PA9) 并把 trace 输出注册到该串口 */
void board_trace_start(uint32_t baud);

/* USART2(PA2/PA3)：ML307 4G 模组 AT 通道；首次调用时初始化硬件 */
struct hal_uart;
struct hal_uart *board_uart2(void);

/* 初始化 ML307（UART2）并注册 mqtt 测试命令；模块未接时命令返回失败 */
void board_net_init(void);

/* 注册板级 RTC 后端：优先 LSE 32.768kHz 硬件 RTC，失败退回 1Hz 软件计时 */
void board_rtc_init(void);

/* 风扇：复用 PWM 编号 0（TIM2_CH1 / PA0）输出，25kHz；注册 roshell 的 fan 命令 */
void board_fan_init(void);

/* 步进电机：STEP=PA4，DIR=PA5；注册 roshell 的 stepper 命令 */
void board_stepper_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_STM32F103CBT6_H */
