#ifndef PORT_STM32F1_H
#define PORT_STM32F1_H

/*
 * STM32F1 平台私有扩展。
 * 公共 API 请包含 "port.h"；本头文件只暴露需要用户中断服务函数转发的入口。
 */

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在 USARTx_IRQHandler() 中调用。 */
void port_stm32f1_uart_irq_handler(port_uart_handle_t *handle);

/* 在 CANx_RX0_IRQHandler()/CANx_TX_IRQHandler() 中调用。 */
void port_stm32f1_can_irq_handler(port_can_handle_t *handle);

/* 在 RTC_Alarm_IRQHandler() 中调用。 */
void port_stm32f1_rtc_alarm_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_STM32F1_H */
