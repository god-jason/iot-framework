/*
 * pc_shell.c - 主机平台 Shell 默认绑定（口令开启，与 STM32 绑定一致）
 *
 *   - 上电只启动一个监听任务，不创建 Shell 实例、不打印 banner；
 *   - 输入 hello 回车 → 回复 hello! → 惰性创建实例与输入任务；
 *   - Shell 常驻，不因空闲自动关闭；输入 exit 回收实例，回到监听状态；
 *   - 日志走 stdout，与 Shell 实例是否存在无关。
 *
 * 输出：stdout（与 log 的 printf 同一路，保证顺序一致）
 * 输入：stdin 按行读取后投递给 shell_feed()
 *     - 管道/重定向输入：读到 EOF 后停止内核（便于脚本化测试）
 *     - 交互输入：终端行缓冲，回车后整行送入 Shell
 *
 * 本文件不参与 STM32 构建。
 */

#if defined(ROS_PLATFORM_PC)

#include "shell.h"
#include "ros.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/select.h>
#endif

#define PC_OPEN_WORD    "hello"
#define PC_OPEN_LEN     5U
#define PC_WATCH_STACK  2048U

static void *s_rx_task;

/* stdin 是否已有数据可读（不阻塞） */
static bool pc_stdin_ready(void)
{
#if defined(_WIN32)
    /* 交互控制台：有按键才算就绪；管道/重定向：fgets 会立即返回数据或 EOF */
    if (_isatty(_fileno(stdin))) {
        return (_kbhit() != 0);
    }
    return true;
#else
    {
        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        return (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0);
    }
#endif
}

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

/* 是否为开启口令（去掉行尾 CR/LF 后与 hello 比较） */
static bool pc_line_is_open(const char *line)
{
    size_t n = strlen(line);

    while (n > 0U && (line[n - 1U] == '\r' || line[n - 1U] == '\n')) {
        n--;
    }
    return (n == PC_OPEN_LEN && memcmp(line, PC_OPEN_WORD, PC_OPEN_LEN) == 0);
}

/*
 * 口令监听 + 输入泵：
 *   未激活时只认整行 hello；激活后逐行投递，exit 后回收实例并回到监听。
 */
static void pc_shell_rx_task(void *arg)
{
    char line[256];

    (void)arg;
    for (;;) {
        if (shell_task_finished()) {
            /* exit 之后输入任务已退出：释放实例，回到监听 */
            (void)shell_shutdown();
            continue;
        }
        if (shell_started() && !shell_active()) {
            /* 任务已创建，等它自己申请好实例缓冲再读下一行 */
            ros_sleep_ms(1U);
            continue;
        }

        /* 没有输入时用 RTOS 睡眠让出 CPU：模拟时间随 idle 继续推进，
         * 否则阻塞在 fgets 里会让整个协作式调度器与时钟一起冻结。 */
        if (!pc_stdin_ready()) {
            ros_sleep_ms(5U);
            continue;
        }
        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* 输入结束（管道 EOF）：先提交未带换行的最后一行，
             * 等 Shell 取完缓冲再停止内核 */
            if (shell_active()) {
                (void)shell_feed("\n", 1U);
                while (shell_pending() > 0U) {
                    ros_sleep_ms(1U);
                }
            }
            ros_stop();
            return;
        }

        if (!shell_started()) {
            if (pc_line_is_open(line)) {
                (void)fputs("hello!\n", stdout);
                (void)fflush(stdout);
                (void)shell_lazy_start();
            }
            continue;               /* 未命中口令：静默丢弃 */
        }

        (void)shell_feed(line, (uint32_t)strlen(line));
        /* 流控：一行处理完再读下一行，避免 Shell 10ms 轮询被输入泵甩开 */
        while (shell_pending() > 0U) {
            ros_sleep_ms(1U);
        }
        if (shell_active()) {
            /* 再让出一次，确保该行命令（可能是 exit）已执行完 */
            ros_sleep_ms(1U);
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
