/*
 * pc_shell.c - 主机平台 Shell 默认绑定（口令开启，与 STM32 绑定一致）
 *
 *   - 上电只启动一个监听任务，不创建 Shell 实例、不打印 banner；
 *   - 输入 hello 回车 → 回复 hello! → 惰性创建实例与输入任务；
 *   - Shell 常驻，不因空闲自动关闭；输入 exit 回收实例，回到监听状态；
 *   - 日志走 stdout，与 Shell 实例是否存在无关。
 *
 * 输出：stdout（与 log 的 printf 同一路，保证顺序一致）
 * 输入：Shell 任务通过默认 stdio（pc_io.c）读取 stdin；
 *       本文件的监听任务只负责口令与实例回收
 *     - 管道/重定向输入：读到 EOF 后 Shell 退出，监听任务随之停止内核
 *     - 交互输入：终端按键直接送入 Shell 行编辑器
 *
 * 本文件不参与 STM32 构建。
 */

#if defined(ROS_PLATFORM_PC)

#include "shell.h"
#include "ros.h"
#include "common.h"

#include <stdio.h>
#include <string.h>

#define PC_OPEN_WORD    "hello"
#define PC_OPEN_LEN     5U
#define PC_WATCH_STACK  2048U

static void *s_rx_task;
static size_t s_open_pos;               /* 口令匹配进度 */

/* 逐字节匹配开启口令；匹配过程中其它字符被丢弃 */
static bool pc_open_word_match(uint8_t ch)
{
    const char *word = PC_OPEN_WORD;

    if (ch == (uint8_t)word[s_open_pos]) {
        s_open_pos++;
    } else {
        s_open_pos = (ch == (uint8_t)word[0]) ? 1U : 0U;
    }
    if (s_open_pos >= PC_OPEN_LEN) {
        s_open_pos = 0U;
        return true;
    }
    return false;
}

/*
 * 口令监听 + 回收：
 *   未激活时从默认 stdio 逐字节匹配 hello；激活后输入由 Shell 任务
 *   通过同一个 stdio 读取，这里只负责在任务退出（exit / 输入 EOF）后
 *   回收实例并回到监听；口令阶段遇到 EOF 则停止内核。
 */
static void pc_shell_rx_task(void *arg)
{
    ros_io_t *io = ros_io_platform_default();
    uint8_t ch;

    (void)arg;
    for (;;) {
        if (shell_task_finished()) {
            /* exit 之后输入任务已退出：释放实例，回到监听 */
            (void)shell_shutdown();
            continue;
        }
        if (shell_started()) {
            ros_sleep_ms(5U);
            continue;
        }

        {
            int n = ros_io_read(io, &ch, 1U);

            if (n < 0) {
                ros_stop();         /* 口令阶段管道 EOF：停止内核 */
                return;
            }
            if (n > 0 && pc_open_word_match(ch)) {
                (void)fputs("hello!\n", stdout);
                (void)fflush(stdout);
                (void)shell_lazy_start();
                continue;
            }
        }
        /* 没有输入时用 RTOS 睡眠让出 CPU：模拟时间随 idle 继续推进，
         * 否则阻塞在内核里会让整个协作式调度器与时钟一起冻结。 */
        ros_sleep_ms(5U);
    }
}

int shell_platform_start(const char *version)
{
    shell_config_t cfg;

    /* 立即刷新，避免管道场景下输出滞后 */
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    memset(&cfg, 0, sizeof(cfg));
    cfg.version = version;
    cfg.reset = NULL;           /* PC 上 reboot 报未支持 */
    /* 输入输出直接使用任务 stdio（默认 pc_io.c 的 stdin / stdout） */

    /* 只保存配置；实例与输入任务等收到 hello 才创建 */
    if (shell_lazy_setup(&cfg) != 0) {
        return -1;
    }
    if (s_rx_task == NULL) {
        s_rx_task = ros_task_create_dynamic("sh_watch",
                                            (uint8_t)SHELL_WATCH_PRIO,
                                            pc_shell_rx_task, NULL,
                                            PC_WATCH_STACK);
    }
    return (s_rx_task != NULL) ? 0 : -1;
}

#endif /* ROS_PLATFORM_PC */
