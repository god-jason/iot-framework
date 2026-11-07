#ifndef IOT_PORT_AVR_H
#define IOT_PORT_AVR_H

/*
 * AVR（ATmega328P 等）平台适配：把 iot_port_* 接口映射到 AVR 寄存器。
 * 需在工程中提供 1ms 节拍源（TIMER 溢出中断累加 g_avr_millis_tick）。
 */

#include "iot_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 由定时器中断累加，供 tick_get 使用（用户需在中断内 ++） */
extern volatile uint32_t g_avr_millis_tick;

int iot_avr_port_init(void);

const iot_port_t *iot_avr_port_get(void);
const iot_port_uart_t *iot_avr_uart0_get(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PORT_AVR_H */