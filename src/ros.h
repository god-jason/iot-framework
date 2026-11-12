#ifndef ROS_H
#define ROS_H

/*
 * ros.h - 轻量实时内核统一入口（纯 C99，默认静态分配）
 *
 * 与 FreeRTOS / RT-Thread 相比：
 *   - 更轻：所有对象（TCB/栈/队列缓冲/定时器）默认由调用者提供，内核零动态分配；
 *           也可选用 ros_*_create()/ros_*_destroy() 在运行期动态增删对象；
 *   - 更聚焦：任务、信号量、互斥量、消息队列、事件组、任务通知、软件定时器；
 *   - 固定优先级抢占 + 同优先级时间片轮转，唤醒高优先级任务即请求切换；
 *   - 优先级继承、栈水位线、运行时统计、tickless 空闲均可编译期裁剪。
 *
 * 使用方只需包含本文件，其余声明按实现文件拆分在同名头文件中
 * （`src/` 下所有文件名统一带 `ros_` 前缀；每个内核对象的结构体
 *   定义在自身头文件中，ros_types.h 只放公共常量与前向声明）：
 *   ros_task.h    任务/TCB      ros_sem.h     信号量     ros_mutex.h   互斥量
 *   ros_queue.h   消息队列      ros_event.h   事件组     ros_timers.h  软件定时器
 *   ros_sbuf.h    流式缓冲      ros_msgq.h    变长消息队列
 *   ros_tick.h    tick/延时     ros_sched.h   调度器     ros_wait.h    等待队列
 *   ros_idle.h    空闲任务      ros_stats.h   运行时统计 ros_assert.h  断言
 *   ros_isr.h     中断上下文 API                         ros_port.h    移植层接口
 *   ros_crash.h   崩溃现场记录  ros_mem.h     内存分配
 *   ros_trace.h   事件跟踪宏    ros_types.h   公共常量/状态码/前向声明
 *   ros_version.h 版本/构建时间/编译配置/许可证
 *   ros_config.h  编译期裁剪开关
 *
 * 典型使用顺序：
 *   ros_init();                       // 初始化内核
 *   ros_task_create(...);             // 创建任务
 *   ros_sched_start();                // 启动调度（通常不返回）
 */

#include "ros_types.h"
#include "ros_mem.h"
#include "ros_assert.h"
#include "ros_crash.h"
#include "ros_task.h"
#include "ros_sem.h"
#include "ros_mutex.h"
#include "ros_queue.h"
#include "ros_sbuf.h"
#include "ros_msgq.h"
#include "ros_event.h"
#include "ros_timers.h"
#include "ros_tick.h"
#include "ros_sched.h"
#include "ros_wait.h"
#include "ros_idle.h"
#include "ros_stats.h"
#include "ros_isr.h"
#include "ros_trace.h"
#include "ros_version.h"

#include "log.h"    /* 内核日志宏 LOG_D/I/W/E */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 内核生命周期（ros.c） ============================== */

/* 初始化内核（就绪表/延时表/定时器表/空闲任务），必须在创建任务前调用 */
void ros_init(void);

/* 启动调度器；Windows 模拟端调用 ros_stop() 后会返回 */
void ros_sched_start(void);

/* 停止调度（仅 Windows 模拟端有意义，返回 main；单片机端通常不调用） */
void ros_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* ROS_H */
