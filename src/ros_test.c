/*
 * ros_test.c - 内核测试聚合入口（对应 ros.c）
 *
 * 各功能测试与实现文件同目录、以 _test.c 结尾，本文件统一调度。
 * 内核初始化与 driver 任务在此创建；所有对象静态分配，
 * driver 通过 sleep/阻塞让出 CPU，驱动其它任务运行，结果确定可复现。
 */

#include "test.h"
#include "ros.h"

void ros_test_task(void);
void ros_test_sem(void);
void ros_test_mutex(void);
void ros_test_queue(void);
void ros_test_timer(void);
void ros_test_event(void);
void ros_test_stats(void);
void ros_test_wait(void);
void ros_test_isr(void);
void ros_test_sched(void);
void ros_test_suspend(void);

static ros_task_t s_driver;
static uint8_t s_driver_stack[8192];

static void driver_entry(void *arg)
{
    (void)arg;

    ros_test_task();
    ros_test_sem();
    ros_test_mutex();
    ros_test_queue();
    ros_test_timer();
    ros_test_event();
    ros_test_stats();
    ros_test_wait();
    ros_test_isr();
    ros_test_sched();
    ros_test_suspend();

    ros_stop();     /* 返回 main */
}

void ut_ros(void)
{
    ros_init();

    TF_CHECK_INT(ros_task_create(&s_driver, "driver", 0, driver_entry, NULL,
                                 s_driver_stack, sizeof(s_driver_stack)),
                 ROS_OK);

    ros_sched_start();
}
