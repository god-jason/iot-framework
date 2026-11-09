/*
 * iot_test.c - 内核测试入口
 *
 * 测试按功能拆分到 tests/iot_test_*.c，由本文件统一调度：
 *   iot_test_task()  任务创建 / tick / 调度 / 退出 / 并行输出
 *   iot_test_sem()   信号量
 *   iot_test_mutex() 互斥量
 *   iot_test_queue() 队列（含生产消费综合）
 *   iot_test_timer() 软件定时器
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
void iot_test_ext(void);

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
    iot_test_ext();

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
