/*
 * app_sched.c - STM32F103CBT6 定时任务演示
 *
 * 注册 3 个演示任务：每天 10:30、每天 10:50、每 30 分钟，各自打印一条日志；
 * 随后常驻，每秒把 RTC 时间喂给 scheduler_process()。
 */

#include "app_sched.h"

#include "ros.h"
#include "ros_rtc.h"
#include "datetime.h"
#include "scheduler.h"

static void sched_task_1030(void *user)
{
    (void)user;
    ros_puts("[sched] task 10:30 fired");
}

static void sched_task_1050(void *user)
{
    (void)user;
    ros_puts("[sched] task 10:50 fired");
}

static void sched_task_30min(void *user)
{
    (void)user;
    ros_puts("[sched] task every 30 min fired");
}

/* 注册 3 个演示任务：每天 10:30、每天 10:50、每 30 分钟 */
static void sched_demo_register(void)
{
    (void)scheduler_init();
    if (scheduler_add("30 10 * * *", sched_task_1030, NULL, NULL) != 0 ||
        scheduler_add("50 10 * * *", sched_task_1050, NULL, NULL) != 0 ||
        scheduler_add("*/30 * * * *", sched_task_30min, NULL, NULL) != 0) {
        ros_puts("sched: add demo tasks failed");
        return;
    }
    ros_puts("sched: 10:30 / 10:50 / every-30min tasks registered");
}

void app_sched_start(void)
{
    sched_demo_register();

    /* app 任务常驻：每秒用 RTC 时间驱动调度器 */
    for (;;) {
        datetime_t now;

        if (timestamp_to_datetime((int64_t)ros_rtc_get_timestamp(), &now) == 0) {
            (void)scheduler_process(&now);
        }
        ros_sleep_ms(1000U);
    }
}
