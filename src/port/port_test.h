#ifndef PORT_TEST_H
#define PORT_TEST_H

/*
 * PORT 测试钩子封装。
 *
 * 目的：ports/<platform>/ 内部的仿真/注入接口不直接被上层测试调用，
 * 统一通过本头文件暴露；具体转发由当前平台的 port_test.c 实现。
 * 非仿真平台提供空实现，保证测试代码可编译。
 */

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 复位平台模拟状态 */
void port_test_reset_state(void);

/* 推进模拟 tick（毫秒） */
void port_test_tick_advance(uint32_t ms);

/* UART 发送捕获与接收注入，返回实际字节数 */
uint32_t port_test_uart_get_tx(port_uart_handle_t *h, uint8_t *buf, uint32_t cap);
uint32_t port_test_uart_inject(port_uart_handle_t *h,
                               const uint8_t *data, uint32_t len);

/* ADC 模拟值注入 */
void port_test_adc_set(uint8_t channel, uint32_t value);
void port_test_adc_set_max(uint8_t channel, uint32_t max);

/* 外部中断触发 */
void port_test_exti_trigger(port_pin_t pin);

/* CAN 接收注入 */
void port_test_can_inject(port_can_handle_t *h, const port_can_frame_t *frame);

/* 捕获/编码器/比较器模拟值 */
void port_test_capture_set(port_capture_handle_t *h, uint32_t value);
void port_test_encoder_set(port_encoder_handle_t *h, int32_t count);
void port_test_comp_set(port_comp_handle_t *h, bool value);

/* I2C 从机寄存器模拟 */
void port_test_i2c_set_mem(uint16_t addr, uint8_t reg, uint8_t value);

/* RTC 时间注入 */
void port_test_time_set(const port_rtc_time_t *t);

/* 看门狗刷新计数与复位调用标记 */
uint32_t port_test_wdg_refresh_count(void);
bool port_test_reset_called(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_TEST_H */
