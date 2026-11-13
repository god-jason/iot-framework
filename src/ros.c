/*
 * ros.c - 内核生命周期
 *
 * 只负责内核初始化与启动/停止，具体实现拆分到：
 *   ros_sched.c   就绪表与调度      ros_wait.c    O(1) 等待队列与阻塞/唤醒
 *   ros_tick.c    tick 与延时链表    ros_timers.c  软件定时器
 *   ros_task.c    任务/通知/注册表   ros_idle.c    空闲任务
 *   ros_sem.c / ros_mutex.c / ros_queue.c / ros_event.c
 *   ros_stats.c / ros_isr.c / ros_assert.c
 */

#include "ros.h"
#include "ros_port.h"

/*
 * 初始化内核。
 * 顺序：崩溃记录 -> 移植层 -> 平台 stdio -> 调度器 -> tick -> 定时器
 *       -> 任务注册表 -> 空闲任务。
 * 必须在创建任务和启动调度之前调用一次。
 */
void ros_init(void)
{
    /* 0. 校验上次复位留下的崩溃现场并安装异常捕获钩子 */
    ros_crash_init();

    /* 1. 移植层初始化（PC 端把主线程转换为 Fiber） */
    ros_port_init();

    /* 1.1 平台默认 stdio：PC 标准输入输出 / STM32 UART1 */
    ros_io_platform_init();

    /* 2. 内核各子系统初始化 */
    ros_trace_init();          /* 事件跟踪（默认运行期关闭） */
    ros_sched_init();
    ros_tick_init();
    ros_timers_init();
    ros_task_module_init();    /* 任务注册表 + 通知等待队列 */

    /* 3. 创建空闲任务（依赖移植层栈初始化） */
    ros_idle_init();

    LOG_I(ROS_TAG, "robotos initialized, max_priority=%d", ROS_MAX_PRIORITY);
}

/*
 * 启动调度器：选择最高优先级就绪任务（没有用户任务时运行空闲任务），
 * 交由移植层 ros_port_start_first() 切入；MCU 端通常不返回。
 */
void ros_sched_start(void)
{
    ros_task_t *first = ros_sched_pick();   /* 无用户任务时返回空闲任务 */

    ros_current_task = first;
    first->state = ROS_TASK_RUNNING;

    LOG_I(ROS_TAG, "scheduler start, first task=%s",
              first->name != NULL ? first->name : "?");

    ros_port_start_first(first);

    /*
     * 单片机端通常不会返回；
     * Windows 模拟端调用 ros_stop() 后由 ros_port_return() 回到这里。
     */
}

/*
 * 停止调度：把控制权交还移植层起点。
 * PC 模拟端回到 ros_sched_start() 之后；MCU 端一般不会调用。
 */
void ros_stop(void)
{
    LOG_I(ROS_TAG, "scheduler stop");
    ros_port_return();
}