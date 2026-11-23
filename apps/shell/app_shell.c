/*
 * app_shell.c - PC 标准控制台进入 roshell（stdin/stdout，口令开启）
 *
 * 本文件强定义 rs_platform_start()，覆盖平台层弱默认实现
 * （platforms/pc/pc_shell.c），策略与之一致：
 *   - 上电只启动一个监听任务，不创建 roshell 实例、不打印 banner；
 *   - 输入 hello 回车 → 回复 hello! → 创建实例与输入任务；
 *   - 会话激活后本任务继续读取 stdin 并逐字节 feed 给会话；
 *   - 输入 exit 回收实例，回到监听状态；
 *   - 日志走 stdout，与实例是否存在无关。
 *
 * 输出：stdout（与 log 的 printf 同一路，保证顺序一致）
 * 输入：本文件的监听任务统一读取 stdin（ros_io_read），
 *       会话未激活时匹配口令，激活后投喂 rs_shell_feed()
 *     - 管道/重定向输入：会话激活时读到 EOF 先关闭会话，
 *       回到监听态后再次 EOF 停止内核
 *     - 交互输入：终端按键逐字节进入会话行编辑器
 */

#if defined(ROS_PLATFORM_PC)

#include "roshell.h"
#include "ros.h"
#include "common.h"

#include <stdio.h>
#include <string.h>

#define STDIO_SHELL_OPEN_WORD   "hello"
#define STDIO_SHELL_OPEN_LEN    5U
#define STDIO_SHELL_STACK       2048U
#define STDIO_SHELL_PRIO        6U      /* 低于会话任务，高于 idle */

static void *s_rx_task;
static const char *s_version;
static size_t s_open_pos;               /* 口令匹配进度 */
static char s_word[8];                  /* 监听态输入的单词（识别 quit/exit） */
static size_t s_word_len;

/* 监听态识别 quit / exit：按回车/换行判定，命中返回 true */
static bool stdio_shell_word_feed(uint8_t ch)
{
    if (ch >= (uint8_t)'A' && ch <= (uint8_t)'Z') {
        ch = (uint8_t)(ch - 'A' + 'a');
    }
    if (ch >= (uint8_t)'a' && ch <= (uint8_t)'z') {
        if (s_word_len + 1U < sizeof(s_word)) {
            s_word[s_word_len++] = (char)ch;
        } else {
            s_word_len = 0U;
        }
        return false;
    }
    if (ch == (uint8_t)'\r' || ch == (uint8_t)'\n') {
        bool stop = (s_word_len == 4U) &&
                    (memcmp(s_word, "quit", 4U) == 0 ||
                     memcmp(s_word, "exit", 4U) == 0);

        s_word_len = 0U;
        return stop;
    }
    s_word_len = 0U;
    return false;
}

/* 逐字节匹配开启口令；匹配过程中其它字符被丢弃 */
static bool stdio_shell_open_word_match(uint8_t ch)
{
    const char *word = STDIO_SHELL_OPEN_WORD;

    if (ch == (uint8_t)word[s_open_pos]) {
        s_open_pos++;
    } else {
        s_open_pos = (ch == (uint8_t)word[0]) ? 1U : 0U;
    }
    if (s_open_pos >= STDIO_SHELL_OPEN_LEN) {
        s_open_pos = 0U;
        return true;
    }
    return false;
}

/*
 * 口令监听 + 输入泵 + 回收：
 *   未激活时逐字节匹配 hello；激活后把 stdin 字节投喂会话；
 *   会话任务退出（exit / EOF 请求）后回收实例并回到监听；
 *   监听阶段遇到 EOF 则停止内核。
 */
static void stdio_shell_rx_task(void *arg)
{
    ros_io_t *io = ros_io_platform_default();
    uint8_t ch;

    (void)arg;
    for (;;) {
        if (rs_shell_task_finished()) {
            /* exit 之后输入任务已退出：释放实例，回到监听 */
            (void)rs_shell_shutdown();
            s_word_len = 0U;
            continue;
        }

        {
            int n = ros_io_read(io, &ch, 1U);

            if (n < 0) {
                if (rs_shell_started()) {
                    /* 会话激活时管道 EOF：关闭会话，下一轮 EOF 停内核。
                     * 必须让出 CPU，否则 EOF 读立即返回会饿死会话任务 */
                    rs_shell_request_exit();
                    ros_sleep_ms(5U);
                    continue;
                }
                ros_stop();         /* 口令阶段管道 EOF：停止内核 */
                return;
            }
            if (n == 0) {
                ros_sleep_ms(5U);   /* 无输入：让出 CPU 给调度器 */
                continue;
            }
            if (rs_shell_started()) {
                rs_shell_feed((const char *)&ch, 1U);
                continue;
            }
            {
                bool stop = stdio_shell_word_feed(ch);

                if (stdio_shell_open_word_match(ch)) {
                    (void)fputs("hello!\n", stdout);
                    (void)fflush(stdout);
                    (void)rs_shell_start(s_version);
                    s_word_len = 0U;
                    continue;
                }
                if (stop) {
                    /* 监听态直接输入 quit / exit：结束进程 */
                    (void)fputs("bye\n", stdout);
                    (void)fflush(stdout);
                    ros_stop();
                    return;
                }
            }
        }
    }
}

int rs_platform_start(const char *version)
{
    /* 立即刷新，避免管道场景下输出滞后 */
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    s_version = version;

    /* 实例与输入任务等收到 hello 才创建 */
    if (s_rx_task == NULL) {
        s_rx_task = ros_task_create_dynamic("sh_watch", STDIO_SHELL_PRIO,
                                            stdio_shell_rx_task, NULL,
                                            STDIO_SHELL_STACK);
    }
    return (s_rx_task != NULL) ? 0 : -1;
}

#endif /* ROS_PLATFORM_PC */
