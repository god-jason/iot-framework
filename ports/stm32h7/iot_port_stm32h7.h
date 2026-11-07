#ifndef IOT_PORT_STM32H7_H
#define IOT_PORT_STM32H7_H

/*
 * STM32H743 平台适配：把 iot_port_* 接口映射到 STM32H7 HAL。
 * 需配合 STM32CubeH7 HAL 库与 CubeMX 生成的 HAL 句柄。
 */

#include "iot_port.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_stm32h7_port_init(void);

const iot_port_t *iot_stm32h7_port_get(void);
const iot_port_uart_t *iot_stm32h7_uart1_get(void);
const iot_port_spi_t *iot_stm32h7_spi1_get(void);
const iot_port_i2c_t *iot_stm32h7_i2c1_get(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PORT_STM32H7_H */