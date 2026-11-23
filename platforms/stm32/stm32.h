#ifndef STM32_H
#define STM32_H

/*
 * STM32 平台私有扩展（F1 / F4 / H7 共用）。
 *
 * 公共 API 请包含 "hal.h"；本头文件只暴露需要用户中断服务函数转发的入口。
 * 编译时必须在目标宏中定义 ROS_STM32_F1、ROS_STM32_F4、ROS_STM32_H7 之一，
 * 对应实现文件才会参与编译：
 *   F1: platforms/stm32f1 下的寄存器实现（gpio / tim）
 *   F4: platforms/stm32f4 下的 HAL 模块（gpio / uart / dma 等）
 *   H7: platforms/stm32h7 下的 HAL2 骨架
 *
 * 说明：
 *   - F1/F4/H7 使用“用户提供 HAL 句柄”模型，中断需由用户 IRQHandler 转发到
 *     下面的 ros_stm32_*_irq_handler()。
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在 USARTx_IRQHandler() 中调用。 */
void ros_stm32_uart_irq_handler(hal_uart_t *handle);

/* 在 CANx_RX0_IRQHandler()/CANx_TX_IRQHandler() 中调用。 */
void ros_stm32_can_irq_handler(hal_can_t *handle);

/* 在 RTC_Alarm_IRQHandler() 中调用。 */
void ros_stm32_rtc_alarm_irq_handler(void);

/*
 * 注册 1Hz 软件 RTC 后端（内核 tick 分频，基准 ROS_RTC_DEFAULT_UNIX）。
 * 供硬件 RTC 不可用时的板级回退使用，多次调用只重新设置基准。
 */
void stm32_rtc_soft_init(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32_H */
