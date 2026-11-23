/*
 * app_main.c - STM32F407VET6 开发板 ML307(4G) 功能演示入口
 *
 * 由板级 main 创建为 app 任务，依次完成：
 *   1. app_device_init()     LED / PWM / 风扇 / 步进 / 心跳任务
 *   2. app_fs_init()         SPI Flash + ROFS 文件系统
 *   3. app_ml307_register()  ML307 演示命令（ml307/sim/ping/http/net/mqtt）
 *
 * 各功能实现见 app_device.c / app_fs.c / app_ml307.c。
 *
 * 注意：F407 的 USART2 走 Cube HAL 轮询收字节（at 引擎已适配），
 *       PA2/PA3 与 PWM 编号 2/3 冲突，接模组时这两路 PWM 不可用。
 */

#include "app.h"
#include "ros.h"
#include "app_device.h"
#include "app_fs.h"
#include "app_ml307.h"

void app_main(void *arg)
{
    (void)arg;

    app_device_init();
    (void)app_fs_init();

    ros_puts("ml307 demo app ready: type 'ml307' to run all tests");

    app_ml307_register();
}
