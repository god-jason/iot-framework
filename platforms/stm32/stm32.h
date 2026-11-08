#ifndef STM32_H
#define STM32_H

/*
 * STM32 平台私有扩展（F1 / F4 / H7 共用）。
 *
 * 公共 API 请包含 "hal.h"；本头文件只暴露需要用户中断服务函数转发的入口。
 * 编译时必须在目标宏中定义 IOT_STM32_F1、IOT_STM32_F4、IOT_STM32_H7 之一，
 * 对应实现文件才会参与编译：
 *   stm32_f1.c / stm32_f4.c / stm32_h7.c
 *
 * 说明：
 *   - F1/F4 使用“用户提供 HAL 句柄”模型，中断需由用户 IRQHandler 转发到
 *     下面的 iot_stm32_*_irq_handler()；
 *   - H7 使用内部槽位池自动分配 HAL 句柄，并通过 HAL 弱回调分发事件，
 *     无需调用下列转发函数。
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在 USARTx_IRQHandler() 中调用。 */
void iot_stm32_uart_irq_handler(hal_uart_t *handle);

/* 在 CANx_RX0_IRQHandler()/CANx_TX_IRQHandler() 中调用。 */
void iot_stm32_can_irq_handler(hal_can_t *handle);

/* 在 RTC_Alarm_IRQHandler() 中调用。 */
void iot_stm32_rtc_alarm_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32_H */
