#ifndef IOT_H
#define IOT_H

/*
 * RTOS —— 轻量实时内核公共接口（纯 C99，静态分配）。
 *
 * 与 FreeRTOS / RT-Thread 相比：
 *   - 更轻：所有对象（TCB/栈/队列缓冲/定时器）由调用者提供，内核 0 次动态分配；
 *   - 更简单：只保留任务、信号量、互斥量、消息队列、软件定时器；
 *   - 固定优先级调度 + 同优先级轮转（通过 iot_yield 让出）。
 *
 * 典型使用顺序：
 *   iot_init();                       // 初始化内核
 *   iot_task_create(...);             // 创建任务
 *   iot_sched_start();                // 启动调度（通常不返回）
 *
 * 类型与结构体定义见 types.h；移植层接口见 port.h。
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 内核生命周期 ============================== */

/* 初始化内核（就绪表/延时表/定时器表/空闲任务），必须在创建任务前调用 */
void iot_init(void);

/* 启动调度器；Windows 模拟端调用 iot_stop() 后会返回 */
void iot_sched_start(void);

/* 停止调度（仅 Windows 模拟端有意义，返回 main；单片机端通常不调用） */
void iot_stop(void);

/* ============================== tick 与调度 ============================== */

/* 获取内核 tick（毫秒累计值，允许回绕） */
uint32_t iot_tick_get(void);

/* 内核 tick 推进 1（唤醒到期任务、触发定时器）；单片机由 SysTick 调用 */
void iot_tick(void);

/* 推进 delta 个 tick；主要用于 Windows 模拟端批量推进时间 */
void iot_tick_increment(uint32_t delta);

/* 让出 CPU，触发同优先级/高优先级调度 */
void iot_yield(void);

/* 获取当前运行任务，可能为 NULL（尚未启动） */
iot_task_t *iot_current(void);

/* ============================== 任务 ============================== */

/*
 * 创建任务。
 *   task       : 任务控制块（调用者静态提供）；
 *   name       : 任务名（只读引用）；
 *   priority   : 优先级 0..IOT_PRIORITY_IDLE-1；
 *   entry      : 任务入口，返回后自动退出；
 *   arg        : 入口参数；
 *   stack_mem  : 任务栈内存（调用者提供，建议 8 字节对齐）；
 *   stack_size : 栈大小（字节）。
 */
iot_status_t iot_task_create(iot_task_t *task, const char *name,
                               uint8_t priority, iot_entry_t entry, void *arg,
                               void *stack_mem, uint32_t stack_size);

/* 退出当前任务（任务主体返回后由移植层自动调用） */
void iot_task_exit(void);

/* 睡眠 ms 个 tick，让出 CPU 直到到期唤醒 */
void iot_sleep_ms(uint32_t ms);

/* 睡眠到绝对 tick 时刻 */
void iot_sleep_until(uint32_t wake_tick);

/* ============================== 信号量 ============================== */

iot_status_t iot_sem_init(iot_sem_t *sem, uint32_t initial_count);
iot_status_t iot_sem_take(iot_sem_t *sem, uint32_t timeout);
iot_status_t iot_sem_give(iot_sem_t *sem);

/* ============================== 互斥量 ============================== */

iot_status_t iot_mutex_init(iot_mutex_t *mutex);
iot_status_t iot_mutex_lock(iot_mutex_t *mutex, uint32_t timeout);
iot_status_t iot_mutex_unlock(iot_mutex_t *mutex);

/* ============================== 消息队列 ============================== */

/*
 * 初始化定长消息队列。
 *   buffer    : 队列缓冲（调用者提供），容量 = item_size * depth；
 *   item_size : 单条消息大小（字节）；
 *   depth     : 队列深度（条数）。
 */
iot_status_t iot_queue_init(iot_queue_t *queue, void *buffer,
                              uint32_t item_size, uint32_t depth);

/* 发送一条消息；队列满且 timeout 不为 0 时阻塞（item 缓冲须保持有效） */
iot_status_t iot_queue_send(iot_queue_t *queue, const void *item,
                              uint32_t timeout);

/* 接收一条消息；队列空且 timeout 不为 0 时阻塞 */
iot_status_t iot_queue_recv(iot_queue_t *queue, void *item,
                              uint32_t timeout);

/* ============================== 软件定时器 ============================== */

iot_status_t iot_timer_init(iot_timer_t *timer, const char *name,
                              iot_timer_cb_t cb, void *arg,
                              uint32_t period_ms, bool periodic);
iot_status_t iot_timer_start(iot_timer_t *timer, uint32_t period_ms);
iot_status_t iot_timer_stop(iot_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif /* IOT_H */