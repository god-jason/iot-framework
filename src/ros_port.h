#ifndef ROS_PORT_H
#define ROS_PORT_H

/*
 * ros_port.h - RTOS 移植层接口
 *
 * 平台相关的上下文切换、栈初始化、临界区集中在这里：
 *   - platforms/pc/pc_rtos.c       : Windows Fiber（协作式，独立栈）；
 *   - platforms/stm32/stm32_rtos.c : Cortex-M PendSV（抢占式，SysTick 驱动）。
 *
 * ros_task_t 的完整布局见 ros_task.h，移植层需要访问
 * task->stack_ptr / entry / arg / stack_mem / stack_size 等字段。
 */

#include "ros.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化移植层（PC 端把主线程转换为 Fiber） */
void ros_port_init(void);

/*
 * 初始化任务栈；完成后写入 task->stack_ptr。
 * 返回 ROS_OK / ROS_NO_MEMORY（上下文构造失败，如 CreateFiber 返回 NULL）。
 */
int ros_port_stack_init(ros_task_t *task);

/*
 * 释放 ros_port_stack_init() 建立的上下文（动态任务销毁用）。
 *   - PC 端：删除 Fiber / 归还 ucontext 槽位；
 *   - Cortex-M/AVR：无独立上下文，空实现。
 * 对静态任务无需调用。
 */
void ros_port_stack_deinit(ros_task_t *task);

/* 启动第一个任务，通常不返回 */
void ros_port_start_first(ros_task_t *task);

/* 保存 from 的上下文，切换到 to 运行 */
void ros_port_switch(ros_task_t *from, ros_task_t *to);

/* 把控制权交还给调度器起点（Windows 模拟端用于 ros_stop） */
void ros_port_return(void);

/*
 * 空闲处理：内核空闲任务在没有任何就绪任务时调用（tickless 关闭时使用）。
 *   - PC 模拟端：把模拟 tick 推进到最近的唤醒/定时截止点；
 *   - MCU 端：进入低功耗（WFI）等待中断。
 */
void ros_port_idle(void);

/*
 * tickless 空闲：按 ms 个 tick 的超时进入低功耗。
 *   - PC 模拟端：等价于把模拟时间推进 ms 个 tick；
 *   - MCU 端：把唤醒定时器（SysTick/RTC）重装为 ms 后进入 WFI。
 * ms 为 UINT32_MAX 表示没有任何截止点。
 */
void ros_port_idle_timeout(uint32_t ms);

/*
 * 请求一次重新调度。
 *   - MCU 端：置位 PendSV，在中断退出后完成切换；
 *   - 协作式 PC 端：空实现。
 * 由 ros_task_wake()（唤醒高优先级任务）与 tick 时间片耗尽路径调用。
 */
void ros_port_request_switch(void);

/*
 * 当前是否处于中断/异常上下文。
 * 内核据此禁止在 ISR / 定时器回调里调用可能阻塞的 API。
 */
bool ros_port_in_isr(void);

/*
 * trace 时间戳（µs，单调，可在 ISR 调用）。
 * ros_trace.c 提供 weak 默认实现（ros_tick_get()*1000），
 * STM32 平台用 DWT->CYCCNT 覆盖以获得 µs 精度。
 */
uint32_t ros_port_trace_time_us(void);

/*
 * 致命错误停机（断言失败等不可恢复错误）。
 * 模拟端可退出进程，MCU 端关闭中断后死循环；该函数不返回。
 */
void ros_port_fatal_halt(void) __attribute__((noreturn));

/*
 * 返回一块复位不清零的内存（容量至少 size 字节），用于崩溃记录持久化。
 * 不支持持久化的平台返回 NULL，内核退化为普通静态区。
 */
void *ros_port_crash_buffer(uint32_t size);

/*
 * 进入/退出临界区，必须支持嵌套（内核存在嵌套调用路径），
 * 典型实现为 PRIMASK 保存/恢复 + 嵌套计数；PC 协作式为可空操作。
 */
void ros_port_critical_enter(void);
void ros_port_critical_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* ROS_PORT_H */
