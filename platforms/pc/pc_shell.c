/*
 * pc_shell.c - 主机平台 Shell 默认绑定
 *
 * 输出：stdout（与 log 的 printf 同一路，保证顺序一致）
 * 输入：stdin，按行读取后投递给 shell_feed()
 *     - 管道/重定向输入：读到 EOF 后停止内核（便于脚本化测试）
 *     - 交互输入：终端行缓冲，回车后整行送入 Shell
 *
 * 调试接口（iot_test_*）仍可用；本文件不参与 STM32 构建。
 */

#if defined(IOT_PLATFORM_PC)

#include "shell.h"
#include "iot.h"

#include <stdio.h>
#include <string.h>

static void *s_rx_task;

/* Shell 输出后端：stdout */
static int pc_shell_write(const char *data, uint32_t len, void *user)
{
    (void)user;
    if (data == NULL || len == 0U) {
        return 0;
    }
    (void)fwrite(data, 1U, (size_t)len, stdout);
    (void)fflush(stdout);
    return (int)len;
}

/* 输入泵：逐行读取 stdin 并投递；EOF 时退出内核 */
static void pc_shell_rx_task(void *arg)
{
    char line[256];

    (void)arg;
    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* 输入结束（管道 EOF）：先提交未带换行的最后一行，
             * 等 Shell 取完缓冲再停止内核 */
            (void)shell_feed("\n", 1U);
            while (shell_pending() > 0U) {
                iot_sleep_ms(1U);
            }
            iot_stop();
            return;
        }
        (void)shell_feed(line, (uint32_t)strlen(line));
        /* 流控：一行处理完再读下一行，避免 Shell 10ms 轮询被输入泵甩开 */
        while (shell_pending() > 0U) {
            iot_sleep_ms(1U);
        }
    }
}

int shell_platform_start(const char *version)
{
    shell_config_t cfg;

    /* 立即刷新，避免管道场景下输出滞后 */
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    memset(&cfg, 0, sizeof(cfg));
    cfg.version = version;
    cfg.write = pc_shell_write;
    cfg.user = NULL;
    cfg.reset = NULL;           /* PC 上 reboot 报未支持 */

    if (shell_setup(&cfg) != 0) {
        return -1;
    }
    if (shell_start() != 0) {
        return -1;
    }
    if (s_rx_task == NULL) {
        /* 优先级低于 shell 任务：先让 shell 任务打印提示符 */
        s_rx_task = iot_task_create_dynamic("uart_rx", 6U, pc_shell_rx_task,
                                            NULL, 4096U);
    }
    return (s_rx_task != NULL) ? 0 : -1;
}

#endif /* IOT_PLATFORM_PC */
