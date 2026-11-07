#ifndef IOT_PORT_SIM_H
#define IOT_PORT_SIM_H

/*
 * Windows 模拟平台：提供 iot_port_t 与一个 stdout UART 实例，供功能测试。
 */

#include "iot_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 获取模拟平台 port（system + uart） */
const iot_port_t *iot_sim_port_get(void);

/* 获取模拟 UART（输出到 stdout，无回环输入） */
const iot_port_uart_t *iot_sim_uart_get(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PORT_SIM_H */