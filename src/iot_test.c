/*
 * iot_test.c - 内核测试聚合入口（对应 iot.c）
 *
 * 各功能测试与实现文件同目录、以 _test.c 结尾，本文件统一调度。
 * 内核初始化与 driver 任务在此创建；所有对象静态分配，
 * driver 通过 sleep/阻塞让出 CPU，驱动其它任务运行，结果确定可复现。
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
void iot_test_suspend(void);

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
    iot_test_suspend();

    iot_stop();     /* 返回 main */
}

void ut_iot(void)
{
    iot_init();

    TF_CHECK_INT(iot_task_create(&s_driver, "driver", 0, driver_entry, NULL,
                                 s_driver_stack, sizeof(s_driver_stack)),
                 IOT_OK);

    iot_sched_start();
}
