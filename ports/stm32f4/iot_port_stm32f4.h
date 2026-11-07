#ifndef IOT_PORT_STM32F4_H
#define IOT_PORT_STM32F4_H

/*
 * STM32F407 平台适配：把 iot_port_* 接口映射到 STM32F4 HAL。
 * 需配合 STM32CubeF4 HAL 库与 CubeMX 生成的 HAL 句柄。
 */

#include "iot_port.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_stm32f4_port_init(void);

const iot_port_t *iot_stm32f4_port_get(void);
const iot_port_uart_t *iot_stm32f4_uart1_get(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PORT_STM32F4_H */