/*
 * app_main.c - STM32F407VET6 开发板 ML307(4G) 功能演示入口
 *
 * 由板级 main 创建为 app 任务，依次完成：
 *   1. app_config_init()     内部 Flash 配置加载/保存（cfg 命令）
 *   2. app_device_init()     LED / 风扇 / 步进 / 心跳任务
 *   3. app_ml307_register()  ML307 演示命令（ml307/sim/ping/http/net/mqtt）
 *   4. app_fs_init()         SPI Flash + MFS 文件系统
 *
 * 命令先于文件系统注册：FS 初始化可能较慢，先保证 Shell 命令可用。
 * 各功能实现见 app_config.c / app_device.c / app_fs.c / app_ml307.c。
 *
 * 注意：F407 的 USART2 走 Cube HAL 轮询收字节（at 引擎已适配），
 *       PA2/PA3 与 PWM 编号 2/3 冲突，接模组时这两路 PWM 不可用。
 */

#include "app.h"
#include "ros.h"
#include "app_config.h"
#include "app_device.h"
#include "app_fs.h"
#include "app_ml307.h"
#include "app_stress.h"
#include "app_memtest.h"
#include "app_timing.h"
#include "app_shelltest.h"

void app_main(void *arg)
{
    (void)arg;

    /* 内部 Flash 配置：加载（或默认）并记录本次启动 */
    (void)app_config_init();

    app_device_init();

    /* 先注册 ML307 演示命令：FS 初始化/自检可能耗时较长，
     * 提前注册可让 Shell 在文件系统就绪前就能使用 ml307/sim/http/net/mqtt */
    app_ml307_register();

    ros_puts("ml307 demo app ready: type 'ml307' to run all tests");

#if APP_MEMTEST_ENABLE
    /* 堆泄漏自检：在压测/文件系统启动前同步执行，避免并发分配干扰 */
    app_memtest_run();
#endif

#if APP_TIMING_ENABLE
    /* ros_sleep_ms 精度测试：压测启动前执行，减少抢占抖动 */
    app_timing_run();
#endif

#if APP_SHELLTEST_ENABLE
    /* roshell 长命令：异步 tick / wait / pause / resume 自检 */
    app_shelltest_run();
#endif

#if APP_STRESS_ENABLE
    /* 稳定性压测：放在文件系统之前，FS 初始化异常也不影响压测运行 */
    app_stress_start();
#endif

    (void)app_fs_init();
}
