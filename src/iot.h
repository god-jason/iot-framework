#ifndef IOT_H
#define IOT_H

/*
 * iot.h - 轻量实时内核统一入口（纯 C99，静态分配）
 *
 * 与 FreeRTOS / RT-Thread 相比：
 *   - 更轻：所有对象（TCB/栈/队列缓冲/定时器）由调用者提供，内核 0 次动态分配；
 *   - 更聚焦：任务、信号量、互斥量、消息队列、事件组、任务通知、软件定时器；
 *   - 固定优先级抢占 + 同优先级时间片轮转，唤醒高优先级任务即请求切换；
 *   - 优先级继承、栈水位线、运行时统计、tickless 空闲均可编译期裁剪。
 *
 * 使用方只需包含本文件，其余声明按实现文件拆分在同名头文件中
 * （`src/` 下所有文件名统一带 `iot_` 前缀；每个内核对象的结构体
 *   定义在自身头文件中，iot_types.h 只放公共常量与前向声明）：
 *   iot_task.h    任务/TCB      iot_sem.h     信号量     iot_mutex.h   互斥量
 *   iot_queue.h   消息队列      iot_event.h   事件组     iot_timers.h  软件定时器
 *   iot_tick.h    tick/延时     iot_sched.h   调度器     iot_wait.h    等待队列
 *   iot_idle.h    空闲任务      iot_stats.h   运行时统计 iot_assert.h  断言
 *   iot_isr.h     中断上下文 API                         iot_port.h    移植层接口
 *   iot_types.h   公共常量/状态码/前向声明  iot_config.h 编译期裁剪开关
 *
 * 典型使用顺序：
 *   iot_init();                       // 初始化内核
 *   iot_task_create(...);             // 创建任务
 *   iot_sched_start();                // 启动调度（通常不返回）
 */

#include "iot_types.h"
#include "iot_assert.h"
#include "iot_task.h"
#include "iot_sem.h"
#include "iot_mutex.h"
#include "iot_queue.h"
#include "iot_event.h"
#include "iot_timers.h"
#include "iot_tick.h"
#include "iot_sched.h"
#include "iot_wait.h"
#include "iot_idle.h"
#include "iot_stats.h"
#include "iot_isr.h"

#include "log.h"    /* 内核日志宏 LOG_D/I/W/E */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 内核生命周期（iot.c） ============================== */

/* 初始化内核（就绪表/延时表/定时器表/空闲任务），必须在创建任务前调用 */
void iot_init(void);

/* 启动调度器；Windows 模拟端调用 iot_stop() 后会返回 */
void iot_sched_start(void);

/* 停止调度（仅 Windows 模拟端有意义，返回 main；单片机端通常不调用） */
void iot_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_H */
