/*
 * iot_test.c - 内核测试入口（对应 iot.c）
 *
 * 各功能测试与实现文件同目录、以 _test.c 结尾，本文件统一调度：
 *   iot_task_test.c    任务创建 / tick / 调度 / 退出 / 并行输出 / 通知
 *   iot_sem_test.c     信号量
 *   iot_mutex_test.c   互斥量与优先级继承
 *   iot_queue_test.c   队列（含生产消费综合）
 *   iot_timers_test.c  软件定时器
 *   iot_event_test.c   事件组
 *   iot_stats_test.c   运行时统计与栈水位线
 *   iot_wait_test.c    等待队列
 *   iot_isr_test.c     中断上下文接口
 *   iot_sched_test.c   抢占判定
 *
 * 内核初始化与 driver 任务在本文件完成；所有对象静态分配，
 * driver 通过 sleep/阻塞让出 CPU，驱动其它任务运行，结果完全确定。
 */

#include "test.h"
#include "iot.h"

void iot_test_task(void);
void iot_test_sem(void);
void iot_test_mutex(void);
void iot_test_queue(void);
void iot_test_timer(void);
void iot_test_event(void);
void iot_test_stats(void);
void iot_test_wait(void);
void iot_test_isr(void);
void iot_test_sched(void);

static iot_task_t s_driver;
static uint8_t s_driver_stack[8192];

static void driver_entry(void *arg)
{
    (void)arg;

    iot_test_task();
    iot_test_sem();
    iot_test_mutex();
    iot_test_queue();
    iot_test_timer();
    iot_test_event();
    iot_test_stats();
    iot_test_wait();
    iot_test_isr();
    iot_test_sched();

    iot_stop();     /* 返回 main */
}

void ut_iot(void)
{
    iot_init();

    TF_CHECK_INT(iot_task_create(&s_driver, "driver", 0, driver_entry, NULL,
                                  s_driver_stack, sizeof(s_driver_stack)),
                 IOT_OK);

    /* 启动调度器；driver_entry 末尾调用 iot_stop() 返回 */
    iot_sched_start();
}
