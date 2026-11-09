#ifndef IOT_H
#define IOT_H

/*
 * iot.h - 轻量实时内核统一入口（纯 C99，静态分配）
 *
 * 与 FreeRTOS / RT-Thread 相比：
 *   - 更轻：所有对象（TCB/栈/队列缓冲/定时器）由调用者提供，内核 0 次动态分配；
 *   - 更简单：只保留任务、信号量、互斥量、消息队列、软件定时器；
 *   - 固定优先级调度 + 同优先级轮转（通过 iot_yield 让出）。
 *
 * 使用方只需包含本文件，其余声明按实现文件拆分在同名头文件中：
 *   task.h    任务        sem.h     信号量      mutex.h   互斥量
 *   queue.h   消息队列    timers.h  软件定时器  tick.h    tick/延时链表
 *   sched.h   调度器      wait.h    阻塞/唤醒   idle.h    空闲任务
 *   types.h   类型与状态码（不含函数）          port.h    移植层接口
 *
 * 典型使用顺序：
 *   iot_init();                       // 初始化内核
 *   iot_task_create(...);             // 创建任务
 *   iot_sched_start();                // 启动调度（通常不返回）
 */

#include "types.h"
#include "task.h"
#include "sem.h"
#include "mutex.h"
#include "queue.h"
#include "timers.h"
#include "tick.h"
#include "sched.h"
#include "wait.h"
#include "idle.h"

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
