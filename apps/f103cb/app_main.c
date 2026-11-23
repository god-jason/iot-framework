/*
 * app_main.c - STM32F103CBT6 开发板 ML307(4G) 演示应用入口
 *
 * 由平台 main 创建为 app 任务，依次完成：
 *   1. app_uart_init()    UART1 日志/shell、UART2 ML307
 *   2. app_device_init()  LED / PWM / 风扇 / 步进 / 心跳任务
 *   3. app_ml307_register() ML307 演示命令（ml307/sim/ntp/http/net/mqtt）
 *   4. app_sched_start()  定时任务演示 + 常驻喂 RTC 时间（不返回）
 *
 * 各功能实现见 app_uart.c / app_device.c / app_ml307.c / app_sched.c，
 * 串口 shell 见 app_shell.c。
 */

#include "app.h"
#include "app_uart.h"
#include "app_device.h"
#include "app_ml307.h"
#include "app_sched.h"

void app_main(void *arg)
{
    (void)arg;

    app_uart_init();
    app_device_init();
    app_ml307_register();
    app_sched_start();          /* 不返回 */
}
