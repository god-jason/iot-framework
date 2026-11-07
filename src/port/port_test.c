/*
 * PORT 测试钩子转发实现。
 *
 * 仿真平台（定义了 PORT_PLATFORM_SIMULATOR）转发到 ports/simulator 的
 * port_sim_* 接口；其他平台提供空实现。
 */
#include "port_test.h"

#if defined(PORT_PLATFORM_SIMULATOR)

#include "port_sim.h"

void port_test_reset_state(void)
{
    port_sim_reset_state();
}

void port_test_tick_advance(uint32_t ms)
{
    port_sim_tick_advance(ms);
}

uint32_t port_test_uart_get_tx(port_uart_handle_t *h, uint8_t *buf, uint32_t cap)
{
    return port_sim_uart_get_tx(h, buf, cap);
}

uint32_t port_test_uart_inject(port_uart_handle_t *h,
                               const uint8_t *data, uint32_t len)
{
    return port_sim_uart_inject(h, data, len);
}

void port_test_adc_set(uint8_t channel, uint32_t value)
{
    port_sim_adc_set(channel, value);
}

void port_test_adc_set_max(uint8_t channel, uint32_t max)
{
    port_sim_adc_set_max(channel, max);
}

void port_test_exti_trigger(port_pin_t pin)
{
    port_sim_exti_trigger(pin);
}

void port_test_can_inject(port_can_handle_t *h, const port_can_frame_t *frame)
{
    port_sim_can_inject(h, frame);
}

void port_test_capture_set(port_capture_handle_t *h, uint32_t value)
{
    port_sim_capture_set(h, value);
}

void port_test_encoder_set(port_encoder_handle_t *h, int32_t count)
{
    port_sim_encoder_set(h, count);
}

void port_test_comp_set(port_comp_handle_t *h, bool value)
{
    port_sim_comp_set(h, value);
}

void port_test_i2c_set_mem(uint16_t addr, uint8_t reg, uint8_t value)
{
    port_sim_i2c_set_mem(addr, reg, value);
}

void port_test_time_set(const port_rtc_time_t *t)
{
    port_sim_time_set(t);
}

uint32_t port_test_wdg_refresh_count(void)
{
    return port_sim_wdg_refresh_count();
}

bool port_test_reset_called(void)
{
    return port_sim_reset_called();
}

#else

/* 非仿真平台：测试钩子为空实现，保证测试代码可链接 */
void port_test_reset_state(void) {}
void port_test_tick_advance(uint32_t ms) { (void)ms; }

uint32_t port_test_uart_get_tx(port_uart_handle_t *h, uint8_t *buf, uint32_t cap)
{
    (void)h;
    (void)buf;
    (void)cap;
    return 0U;
}

uint32_t port_test_uart_inject(port_uart_handle_t *h,
                               const uint8_t *data, uint32_t len)
{
    (void)h;
    (void)data;
    (void)len;
    return 0U;
}

void port_test_adc_set(uint8_t channel, uint32_t value)
{
    (void)channel;
    (void)value;
}

void port_test_adc_set_max(uint8_t channel, uint32_t max)
{
    (void)channel;
    (void)max;
}

void port_test_exti_trigger(port_pin_t pin) { (void)pin; }
void port_test_can_inject(port_can_handle_t *h, const port_can_frame_t *frame)
{
    (void)h;
    (void)frame;
}
void port_test_capture_set(port_capture_handle_t *h, uint32_t value)
{
    (void)h;
    (void)value;
}
void port_test_encoder_set(port_encoder_handle_t *h, int32_t count)
{
    (void)h;
    (void)count;
}
void port_test_comp_set(port_comp_handle_t *h, bool value)
{
    (void)h;
    (void)value;
}
void port_test_i2c_set_mem(uint16_t addr, uint8_t reg, uint8_t value)
{
    (void)addr;
    (void)reg;
    (void)value;
}
void port_test_time_set(const port_rtc_time_t *t) { (void)t; }
uint32_t port_test_wdg_refresh_count(void) { return 0U; }
bool port_test_reset_called(void) { return false; }

#endif
