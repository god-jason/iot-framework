/*
 * shell_demo.c - PC 平台 IoT Shell 交互示例
 *
 * shell_platform_start() 完成默认绑定：
 *   - 输出：stdout（与 log 的 printf 同一路）
 *   - 输入：stdin，逐行投递到 shell_feed()
 * 输入结束（管道 EOF）或 quit 命令后停止内核，进程正常返回。
 *
 * 交互测试：
 *   xmake r iot-shell
 * 管道测试：
 *   "ver`nprint 1+2`nquit`n" | iot-shell.exe
 */

#include "shell.h"
#include "iot.h"

#include <stdio.h>

static int cmd_quit(shell_t *sh, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_printf(sh, "bye\n");
    iot_stop();
    return SHELL_OK;
}

int main(void)
{
    static const shell_cmd_t quit_cmd = {
        "quit", cmd_quit, "quit", "stop the shell demo", "demo", CMD_NONE
    };

    iot_init();
    if (shell_platform_start("pc-demo") != 0) {
        printf("shell start failed\n");
        return 1;
    }
    if (shell_register_cmd(&quit_cmd) != 0) {
        printf("register quit failed\n");
    }

    iot_sched_start();          /* iot_stop() 后返回 */

    printf("shell demo finished\n");
    return 0;
}
