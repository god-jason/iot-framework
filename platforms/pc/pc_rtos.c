/*
 * pc_rtos.c - RTOS 的 Windows 移植层（协作式，基于 Fiber）
 *
 * 用 Windows Fiber 提供真实的独立任务栈与上下文切换：
 *   - iot_port_init       : 把主线程转换为 Fiber；
 *   - iot_port_stack_init : 为每个任务 CreateFiber；
 *   - iot_port_switch     : SwitchToFiber；
 *   - iot_port_return     : 切回主 Fiber（供 iot_stop 返回 main）。
 *
 * 协作式模型下没有硬件抢占，时间推进由空闲任务调用 iot_port_idle()，
 * 通过 iot_tick_increment() 批量推进到下一个唤醒/定时截止点。
 */

#if defined(IOT_PLATFORM_PC)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "iot.h"
#include "port.h"
#include "iot.h"

static LPVOID s_main_fiber;

/* Fiber 起始例程：运行任务入口，返回后退出该任务 */
static void WINAPI fiber_proc(void *param)
{
    iot_task_t *task = (iot_task_t *)param;

    if (task->entry != NULL) {
        task->entry(task->arg);
    }
    iot_task_exit();
}

void iot_port_init(void)
{
    if (s_main_fiber == NULL) {
        s_main_fiber = ConvertThreadToFiber(NULL);
    }
}

void iot_port_stack_init(iot_task_t *task)
{
    task->stack_ptr = CreateFiber((SIZE_T)task->stack_size,
                                  fiber_proc, (LPVOID)task);
}

void iot_port_start_first(iot_task_t *task)
{
    SwitchToFiber(task->stack_ptr);
}

void iot_port_switch(iot_task_t *from, iot_task_t *to)
{
    (void)from;                  /* Fiber 由系统保存上下文 */
    SwitchToFiber(to->stack_ptr);
}

void iot_port_return(void)
{
    if (s_main_fiber != NULL) {
        SwitchToFiber(s_main_fiber);
    }
}

/*
 * 模拟端空闲处理：把模拟时间推进到最近的唤醒/定时截止点。
 *   - 没有任何时间点时每次推进 1 tick，避免空转过快；
 *   - 存在未来时间点时直接跳到该时刻；
 *   - 已到期时推进 1 tick 触发处理。
 */
void iot_port_idle(void)
{
    uint32_t now = iot_tick_count;
    uint32_t delayed = iot_delayed_next_deadline();
    uint32_t timer = iot_timer_next_deadline();
    uint32_t next = (delayed < timer) ? delayed : timer;

    if (next == UINT32_MAX) {
        iot_tick_increment(1U);
    } else if (IOT_TICK_AFTER(next, now)) {
        iot_tick_increment(next - now);
    } else {
        iot_tick_increment(1U);
    }
}

void iot_port_critical_enter(void)
{
    /* 单线程协作式，无抢占，临界区为空 */
}

void iot_port_critical_exit(void)
{
    /* 单线程协作式，无抢占，临界区为空 */
}

#endif /* IOT_PLATFORM_PC */