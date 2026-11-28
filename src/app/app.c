/*
 * app.c - 应用入口弱定义与退出码存储
 *
 * app_main() 由板级 main() 创建为独立任务；构建目标链接
 * apps/<name>/app_main.c 时由强定义覆盖本实现。
 * 未链接应用时周期性打印提示并原地等待，提醒开发者接入 app_main。
 */

#include "ros.h"
#include "app.h"

/* 应用退出码：任务内写入，板级 main 在调度器返回后读取 */
static int s_exit_code;

__attribute__((weak)) void app_main(void *arg)
{
    (void)arg;

    for (;;) {
        ros_printf("[app] app_main not linked, please link apps/<name>/app_main.c\n");
        ros_sleep_ms(1000U);
    }
}

void app_set_exit_code(int code)
{
    s_exit_code = code;
}

int app_get_exit_code(void)
{
    return s_exit_code;
}
