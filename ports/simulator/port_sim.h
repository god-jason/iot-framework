#ifndef PORT_SIM_H
#define PORT_SIM_H

/*
 * 主机模拟平台（Host Simulator）测试辅助接口。
 * 仅用于单元测试向模拟外设注入数据或查询内部状态，不属于公共 PORT API。
 */

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 复位全部模拟静态状态 */
void port_sim_reset_state(void);

/* 推进逻辑时钟（ms） */
void port_sim_tick_advance(uint32_t ms);

/* UART：读取已发送数据 / 注入接收数据 */
uint32_t port_sim_uart_get_tx(port_uart_handle_t *h, uint8_t *buf, uint32_t cap);
uint32_t port_sim_uart_inject(port_uart_handle_t *h, const uint8_t *data, uint32_t len);

/* ADC：设置通道采样值与量程 */
void port_sim_adc_set(uint8_t channel, uint32_t value);
void port_sim_adc_set_max(uint8_t channel, uint32_t max);

/* EXTI：触发一次外部中断 */
void port_sim_exti_trigger(port_pin_t pin);

/* CAN：注入接收帧 */
void port_sim_can_inject(port_can_handle_t *h, const port_can_frame_t *frame);

/* CAPTURE / ENCODER / COMP 注入 */
void port_sim_capture_set(port_capture_handle_t *h, uint32_t value);
void port_sim_encoder_set(port_encoder_handle_t *h, int32_t count);
void port_sim_comp_set(port_comp_handle_t *h, bool value);

/* I2C：设置从机寄存器内容 */
void port_sim_i2c_set_mem(uint16_t addr, uint8_t reg, uint8_t value);

/* RTC：直接设置软件时间（不校验） */
void port_sim_time_set(const port_rtc_time_t *t);

/* WDG / 系统复位查询 */
uint32_t port_sim_wdg_refresh_count(void);
bool port_sim_reset_called(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SIM_H */
