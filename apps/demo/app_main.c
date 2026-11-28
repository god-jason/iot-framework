/* robotos 综合示例：LED 闪烁 + roshell 命令行。
 *
 * 本文件实现 app_main()，由 platforms/pc/pc_main.c 创建为独立任务；
 * 创建 LED 任务与 roshell 会话后返回（任务结束），应用靠这些任务继续运行；
 * 支持 exit（退回监听）与 quit（结束进程，ros_stop 返回 main）。
 */
#include "ros.h"
#include "common.h"
#include "roshell.h"
#include "roshell_repl.h"
#include "led.h"
#include "app.h"

#include <stdio.h>

static ros_task_t s_led_task;
static uint8_t s_led_stack[1024];
static led_t s_led;

/* LED 每 500ms 翻转一次 */
static void led_entry(void *arg)
{
    (void)arg;
    for (;;) {
        led_toggle(&s_led);
        ros_sleep_ms(500U);
    }
}

/* quit：结束演示进程 */
static int demo_quit(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)parsed;
    (void)user;
    ros_puts("bye");
    ros_stop();
    return 0;
}

static const rs_cmd_t s_quit_def = {
    "quit", "stop demo", NULL, NULL, NULL, 0U, demo_quit, NULL,
    NULL, NULL, NULL
};

void app_main(void *arg)
{
    (void)arg;

    led_init(&s_led, 0, 1);

    printf("robotos demo ready\n");

    (void)rs_cmd_register(&s_quit_def);
    if (rs_platform_start("pc-demo") != 0) {
        printf("roshell start failed\n");
        app_set_exit_code(1);
        return;
    }

    (void)ros_task_create(&s_led_task, "led", 5U, led_entry, NULL,
                          s_led_stack, sizeof(s_led_stack));
}
