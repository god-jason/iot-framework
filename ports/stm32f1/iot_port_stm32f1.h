#ifndef IOT_PORT_STM32F1_H
#define IOT_PORT_STM32F1_H

/*
 * STM32F103 平台适配：把 iot_port_* 接口映射到 STM32F1 HAL。
 * 需配合 STM32CubeF1 HAL 库与对应 CubeMX 生成的 HAL 句柄。
 */

#include "iot_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化平台（时钟/GPIO/USART），返回端口集合指针 */
int iot_stm32f1_port_init(void);

/* 获取已初始化的 port 与主串口 USART1 */
const iot_port_t *iot_stm32f1_port_get(void);
const iot_port_uart_t *iot_stm32f1_uart1_get(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PORT_STM32F1_H */