#ifndef PC_H
#define PC_H

/*
 * PC 主机平台（Host PC）测试辅助接口。
 * 仅用于单元测试向虚拟外设注入数据或查询内部状态，不属于公共 IOT API。
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 复位全部模拟静态状态 */
void iot_pc_reset_state(void);

/* 推进逻辑时钟（ms） */
void iot_pc_tick_advance(uint32_t ms);

/* UART：读取已发送数据 / 注入接收数据 */
uint32_t iot_pc_uart_get_tx(hal_uart_t *h, uint8_t *buf, uint32_t cap);
uint32_t iot_pc_uart_inject(hal_uart_t *h, const uint8_t *data, uint32_t len);

/* ADC：设置通道采样值与量程 */
void iot_pc_adc_set(uint8_t channel, uint32_t value);
void iot_pc_adc_set_max(uint8_t channel, uint32_t max);

/* EXTI：触发一次外部中断 */
void iot_pc_exti_trigger(hal_pin_t pin);

/* CAN：注入接收帧 */
void iot_pc_can_inject(hal_can_t *h, const hal_can_frame_t *frame);

/* CAPTURE / ENCODER 注入 */
void iot_pc_capture_set(hal_capture_t *h, uint32_t value);
void iot_pc_encoder_set(hal_encoder_t *h, int32_t count);

/* I2C：设置从机寄存器内容 */
void iot_pc_i2c_set_mem(uint16_t addr, uint8_t reg, uint8_t value);

/* RTC：直接设置软件时间（不校验） */
void iot_pc_time_set(const hal_rtc_time_t *t);

/* WDG / 系统复位查询 */
uint32_t iot_pc_wdg_refresh_count(void);
bool iot_pc_reset_called(void);

/* 内部接口：复位系统级状态（tick/中断/复位/睡眠标记），由 pc.c 调用 */
void iot_pc_os_reset_state(void);

/* 内部接口：复位 UART1 与全部虚拟串口，由 iot_pc_reset_state() 调用 */
void pc_uart_reset(void);

/* ---------------- 测试钩子（跨平台测试统一入口） ---------------- */
/*
 * 上层测试只包含本头文件并调用 iot_test_*，由 pc.c 转发到 iot_pc_*；
 * 非仿真平台不提供这些接口，测试仅在仿真目标中编译。
 */

/* 复位平台模拟状态 */
void iot_test_reset_state(void);

/* 推进模拟 tick（毫秒） */
void iot_test_tick_advance(uint32_t ms);

/* UART 发送捕获与接收注入，返回实际字节数 */
uint32_t iot_test_uart_get_tx(hal_uart_t *h, uint8_t *buf, uint32_t cap);
uint32_t iot_test_uart_inject(hal_uart_t *h,
                              const uint8_t *data, uint32_t len);

/* ADC 模拟值注入 */
void iot_test_adc_set(uint8_t channel, uint32_t value);
void iot_test_adc_set_max(uint8_t channel, uint32_t max);

/* 外部中断触发 */
void iot_test_exti_trigger(hal_pin_t pin);

/* CAN 接收注入 */
void iot_test_can_inject(hal_can_t *h, const hal_can_frame_t *frame);

/* 捕获/编码器模拟值 */
void iot_test_capture_set(hal_capture_t *h, uint32_t value);
void iot_test_encoder_set(hal_encoder_t *h, int32_t count);

/* I2C 从机寄存器模拟 */
void iot_test_i2c_set_mem(uint16_t addr, uint8_t reg, uint8_t value);

/* RTC 时间注入 */
void iot_test_time_set(const hal_rtc_time_t *t);

/* 看门狗刷新计数与复位调用标记 */
uint32_t iot_test_wdg_refresh_count(void);
bool iot_test_reset_called(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_H */
