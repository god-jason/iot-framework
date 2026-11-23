#ifndef APP_SCHED_H
#define APP_SCHED_H

/*
 * app_sched.h - STM32F103CBT6 定时任务演示
 *
 * 注册 3 个演示任务并常驻：每秒用 RTC 时间驱动 scheduler_process()。
 * 本函数不返回。
 */

#ifdef __cplusplus
extern "C" {
#endif

void app_sched_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHED_H */
