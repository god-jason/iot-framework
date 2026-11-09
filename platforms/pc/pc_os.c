/*
 * 主机模拟平台 —— 系统级接口（hal_mcu.h 实现）。
 *
 * tick 由软件推进，中断/复位/睡眠仅记录状态，便于在 PC 上做单元测试：
 *   - 默认 hal_delay_ms/us 只累加 tick，不真正等待，保证测试快速；
 *   - 定义 IOT_PC_REAL_DELAY 时，hal_delay_ms 会真实等待（用于 demo/交互程序）；
 *   - hal_system_reset 只置标记，可由 iot_test_reset_called() 查询；
 *   - sleep/deepsleep 只置标记，立即返回。
 */

#include "pc.h"

#if defined(IOT_PC_REAL_DELAY)
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif
#endif

/* ============================== 系统状态 ============================== */

static uint32_t s_tick;                 /* 毫秒时基 */
static bool s_irq_enabled = true;       /* 全局中断开关状态 */
static bool s_reset_called;             /* 是否调用过软件复位 */
static bool s_sleep_called;             /* 是否进入过浅睡眠 */
static bool s_deepsleep_called;         /* 是否进入过深度睡眠 */

/* ============================== 时基与延时 ============================== */

uint32_t hal_tick_get(void)
{
    return s_tick;
}

void hal_delay_ms(uint32_t ms)
{
#if defined(IOT_PC_REAL_DELAY)
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    (void)nanosleep(&ts, NULL);
#endif
#endif
    s_tick += ms;
}

void hal_delay_us(uint32_t us)
{
    if (us > 0U) {
        s_tick += (us + 999U) / 1000U;
    }
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

void iot_pc_tick_advance(uint32_t ms)
{
    s_tick += ms;
}

bool iot_pc_reset_called(void)
{
    return s_reset_called;
}

void iot_pc_os_reset_state(void)
{
    s_tick = 0U;
    s_irq_enabled = true;
    s_reset_called = false;
    s_sleep_called = false;
    s_deepsleep_called = false;
}
