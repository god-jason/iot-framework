/*
 * 主机模拟平台 —— 系统级接口（port_os.h 实现）。
 *
 * tick 由软件推进，中断/复位/睡眠仅记录状态，便于在 PC 上做单元测试：
 *   - port_delay_ms/us 直接累加 tick，不真正等待；
 *   - port_system_reset 只置标记，可由 port_test_reset_called() 查询；
 *   - sleep/deepsleep 只置标记，立即返回。
 */

#include "port_sim.h"

/* ============================== 系统状态 ============================== */

static uint32_t s_tick;                 /* 毫秒时基 */
static bool s_irq_enabled = true;       /* 全局中断开关状态 */
static bool s_reset_called;             /* 是否调用过软件复位 */
static bool s_sleep_called;             /* 是否进入过浅睡眠 */
static bool s_deepsleep_called;         /* 是否进入过深度睡眠 */

/* ============================== 时基与延时 ============================== */

uint32_t port_tick_get(void)
{
    return s_tick;
}

void port_delay_ms(uint32_t ms)
{
    s_tick += ms;
}

void port_delay_us(uint32_t us)
{
    if (us > 0U) {
        s_tick += (us + 999U) / 1000U;
    }
}

/* ============================== 中断与复位 ============================== */

void port_irq_enable(void)
{
    s_irq_enabled = true;
}

void port_irq_disable(void)
{
    s_irq_enabled = false;
}

void port_system_reset(void)
{
    s_reset_called = true;
}

void port_system_sleep(void)
{
    s_sleep_called = true;
}

void port_system_deepsleep(void)
{
    s_deepsleep_called = true;
}

/* ============================== 测试辅助 ============================== */

void port_sim_tick_advance(uint32_t ms)
{
    s_tick += ms;
}

bool port_sim_reset_called(void)
{
    return s_reset_called;
}

void port_sim_os_reset_state(void)
{
    s_tick = 0U;
    s_irq_enabled = true;
    s_reset_called = false;
    s_sleep_called = false;
    s_deepsleep_called = false;
}
