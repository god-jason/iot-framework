/*
 * iot.c - 内核生命周期
 *
 * 只负责内核初始化与启动/停止，具体实现拆分到：
 *   iot_sched.c   就绪表与调度      iot_wait.c    O(1) 等待队列与阻塞/唤醒
 *   iot_tick.c    tick 与延时链表    iot_timers.c  软件定时器
 *   iot_task.c    任务/通知/注册表   iot_idle.c    空闲任务
 *   iot_sem.c / iot_mutex.c / iot_queue.c / iot_event.c
 *   iot_stats.c / iot_isr.c / iot_assert.c
 */

#include "iot.h"
#include "iot_port.h"

void iot_init(void)
{
    /* 0. 校验上次复位留下的崩溃现场并安装异常捕获钩子 */
    iot_crash_init();

    /* 1. 移植层初始化（PC 端把主线程转换为 Fiber） */
    iot_port_init();

    /* 2. 内核各子系统初始化 */
    iot_sched_init();
    iot_tick_init();
    iot_timers_init();
    iot_task_module_init();    /* 任务注册表 + 通知等待队列 */

    /* 3. 创建空闲任务（依赖移植层栈初始化） */
    iot_idle_init();

    LOG_I(IOT_TAG, "iot initialized, max_priority=%d", IOT_MAX_PRIORITY);
}

void iot_sched_start(void)
{
    iot_task_t *first = iot_sched_select();

    if (first == NULL) {
        first = iot_idle_task;    /* 没有用户任务时只运行空闲任务 */
    }

    iot_current_task = first;
    first->state = IOT_TASK_RUNNING;

    LOG_I(IOT_TAG, "scheduler start, first task=%s",
              first->name != NULL ? first->name : "?");

    iot_port_start_first(first);

    /*
     * 单片机端通常不会返回；
     * Windows 模拟端调用 iot_stop() 后由 iot_port_return() 回到这里。
     */
}

void iot_stop(void)
{
    LOG_I(IOT_TAG, "scheduler stop");
    iot_port_return();
}