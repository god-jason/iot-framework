#ifndef IOT_PORT_H
#define IOT_PORT_H

/*
 * RTOS 移植层接口。
 *
 * 平台相关的上下文切换、栈初始化、临界区集中在这里：
 *   - platforms/pc/pc_rtos.c      : Windows Fiber（协作式，独立栈）；
 *   - platforms/stm32/stm32_rtos.c: Cortex-M PendSV（抢占式，SysTick 驱动）。
 *
 * iot_task_t 的完整布局见 internal.h，移植层需要访问
 * task->stack_ptr / entry / arg / stack_mem / stack_size 等字段。
 */

#include "iot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化移植层（PC 端把主线程转换为 Fiber） */
void iot_port_init(void);

/* 初始化任务栈；完成后写入 task->stack_ptr */
void iot_port_stack_init(iot_task_t *task);

/* 启动第一个任务，通常不返回 */
void iot_port_start_first(iot_task_t *task);

/* 保存 from 的上下文，切换到 to 运行 */
void iot_port_switch(iot_task_t *from, iot_task_t *to);

/* 把控制权交还给调度器起点（Windows 模拟端用于 iot_stop） */
void iot_port_return(void);

/* 进入/退出临界区（可嵌套），PC 端为空操作 */
void iot_port_critical_enter(void);
void iot_port_critical_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PORT_H */