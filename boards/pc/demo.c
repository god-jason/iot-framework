/* robotos 综合示例：LED 闪烁 + roshell 命令行。
 *
 * 上电只运行 LED 任务和监听任务，输入 hello 后创建 roshell 会话；
 * 支持 exit（退回监听）与 quit（结束进程）。
 */
#include "ros.h"
#include "common.h"
#include "roshell.h"
#include "roshell.h"
#include "led.h"

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
static int demo_quit(const rs_cmd_ctx_t *ctx)
{
    (void)ctx;
    ros_puts("bye");
    ros_stop();
    return 0;
}

static const rs_cmd_t s_quit_def = {
    "quit", "stop demo", NULL, NULL, NULL, 0U, demo_quit, NULL
};

int main(void)
{
    ros_init();
    led_init(&s_led, 0, 1);

    printf("robotos demo ready\n");

    (void)rs_cmd_register(&s_quit_def);
    if (rs_platform_start("pc-demo") != 0) {
        printf("roshell start failed\n");
        return 1;
    }

    (void)ros_task_create(&s_led_task, "led", 5U, led_entry, NULL,
                          s_led_stack, sizeof(s_led_stack));

    ros_sched_start();          /* ros_stop() 后返回 */

    printf("\ndemo finished\n");
    return 0;
}
