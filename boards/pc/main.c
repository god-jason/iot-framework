/*
 * main.c - PC 平台入口
 *
 * 只做内核初始化：把应用入口 app_main() 创建为独立任务，再启动调度器。
 *   apps/demo/app_main.c   综合示例（ros-demo）
 *   apps/shell/app_main.c  Shell 交互示例（ros-shell）
 *   apps/test/app_main.c   单元测试（ros-test）
 *
 * 应用调用 ros_stop() 后调度器返回，这里读取应用留下的退出码作为进程退出码。
 */

#include "ros.h"
#include "app.h"

/* 测试类应用（单元测试）需要较大的栈 */
#define APP_TASK_STACK   8192U
#define APP_TASK_PRIO    0U

static ros_task_t s_app_task;
static uint8_t    s_app_stack[APP_TASK_STACK];

int main(void)
{
    ros_init();

    if (ros_task_create(&s_app_task, "app", APP_TASK_PRIO, app_main, NULL,
                        s_app_stack, sizeof(s_app_stack)) != ROS_OK) {
        return 1;
    }

    ros_sched_start();          /* 应用 ros_stop() 后返回 */

    return app_get_exit_code();
}
