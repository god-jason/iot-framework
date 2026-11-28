/*
 * 主机模拟平台 —— 系统级接口（hal_mcu.h 实现）。
 *
 * tick 由软件推进，中断/复位/睡眠仅记录状态，便于在 PC 上做单元测试：
 *   - 延时统一使用内核 ros_sleep_ms()（由调度器推进 tick）；
 *   - hal_system_reset 只置标记，可由 ros_test_reset_called() 查询；
 *   - sleep/deepsleep 只置标记，立即返回。
 */

#include "pc.h"

/* ============================== 系统状态 ============================== */

static uint32_t s_tick;                 /* 毫秒时基 */
static uint32_t s_tick_us;              /* 毫秒时基的小数部分（微秒） */
static bool s_irq_enabled = true;       /* 全局中断开关状态 */
static bool s_reset_called;             /* 是否调用过软件复位 */
static bool s_sleep_called;             /* 是否进入过浅睡眠 */
static bool s_deepsleep_called;         /* 是否进入过深度睡眠 */

/* ============================== 时基 ============================== */

uint32_t hal_tick_get(void)
{
    return s_tick;
}

/* ============================== 中断与复位 ============================== */

void hal_irq_enable(void)
{
    s_irq_enabled = true;
}

void hal_irq_disable(void)
{
    s_irq_enabled = false;
}

uint32_t hal_irq_save(void)
{
    uint32_t prev = s_irq_enabled ? 1U : 0U;

    s_irq_enabled = false;
    return prev;
}

void hal_irq_restore(uint32_t state)
{
    s_irq_enabled = (state != 0U);
}

/* 单线程模拟：推进模拟时基的微秒量，保证位带驱动的超时判断按真实时间走。 */
void hal_delay_us(uint32_t us)
{
    s_tick_us += us;
    while (s_tick_us >= 1000U) {
        s_tick_us -= 1000U;
        s_tick++;
    }
}

void hal_system_reset(void)
{
    s_reset_called = true;
}

void hal_system_sleep(void)
{
    s_sleep_called = true;
}

void hal_system_deepsleep(void)
{
    s_deepsleep_called = true;
}

/* ============================== 测试辅助 ============================== */

void ros_pc_tick_advance(uint32_t ms)
{
    s_tick += ms;
}

bool ros_pc_reset_called(void)
{
    return s_reset_called;
}

void ros_pc_os_reset_state(void)
{
    s_tick = 0U;
    s_irq_enabled = true;
    s_reset_called = false;
    s_sleep_called = false;
    s_deepsleep_called = false;
}
