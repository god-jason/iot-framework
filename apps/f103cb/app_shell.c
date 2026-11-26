/*
 * app_shell.c - STM32F103CBT6 串口进入 roshell（UART1，口令开启）
 *
 * 本文件强定义 rs_platform_start()，覆盖平台层弱默认实现
 * （platforms/stm32/stm32_shell.c），策略与之一致：
 *   - 上电只创建一个 1024B 栈的监听任务，不分配任何 roshell 缓冲；
 *   - 串口收到口令 "hello"（连续 5 个字符，不需要回车）时回复 "hello!"，
 *     并创建 roshell 实例与输入任务；
 *   - 会话激活后本任务继续轮询 UART 并逐字节 feed 给会话；
 *   - 会话常驻，不会因为长时间无操作自动关闭；
 *   - 输入 exit 命令：打印 bye，输入任务退出，
 *     监听任务回收实例内存，重新回到只监听口令的状态。
 *
 * 输出：会话任务通过默认 stdio（stm32_io.c，UART1）写出
 * 输入：本任务 hal_uart_receive 轮询读取后 feed
 */

#if defined(ROS_PLATFORM_STM32)

#include "roshell.h"
#include "ros.h"
#include "common.h"
#include "hal_uart.h"

#include <stddef.h>
#include <string.h>

/* weak 引用：未链接 HAL/板级实现时仍可编译 */
extern int hal_uart_transmit(hal_uart_t *handle, const uint8_t *data,
                             size_t length, uint32_t timeout)
    __attribute__((weak));
extern int hal_uart_receive(hal_uart_t *handle, uint8_t *data,
                            size_t length, uint32_t timeout)
    __attribute__((weak));

/* 板级约定：未提供 board_shell_uart() 时用非空句柄 1 表示 UART1 */
#define UART_SHELL_DEFAULT ((hal_uart_t *)(void *)1)

/* 板级可选钩子：返回实际使用的 UART 句柄（Cube HAL 下必须是有效句柄） */
extern hal_uart_t *board_shell_uart(void) __attribute__((weak));

#define UART_SHELL_OPEN_WORD    "hello"
#define UART_SHELL_OPEN_LEN     5U
#define UART_SHELL_PRIO         6U          /* 低于会话任务，高于 idle */
#define UART_SHELL_STACK        1024U

static hal_uart_t *uart_shell_uart(void)
{
    if (board_shell_uart != NULL) {
        hal_uart_t *handle = board_shell_uart();

        if (handle != NULL) {
            return handle;
        }
    }
    return UART_SHELL_DEFAULT;
}

static void *s_watch_task;
static roshell_t *s_shell;                   /* 当前会话句柄（监听态为 NULL） */
static const char *s_version;
static size_t s_open_pos;               /* 口令匹配进度 */

static int uart_shell_write(const char *data, uint32_t len, void *user)
{
    (void)user;
    if (data == NULL || len == 0U) {
        return 0;
    }
    if (hal_uart_transmit == NULL) {
        return -1;
    }
    return (hal_uart_transmit(uart_shell_uart(), (const uint8_t *)data,
                              (size_t)len, 100U) == HAL_OK) ? (int)len : -1;
}

/* 逐字节匹配开启口令；匹配过程中其它字符被丢弃 */
static bool uart_shell_open_word_match(uint8_t ch)
{
    const char *word = UART_SHELL_OPEN_WORD;

    if (ch == (uint8_t)word[s_open_pos]) {
        s_open_pos++;
    } else {
        s_open_pos = (ch == (uint8_t)word[0]) ? 1U : 0U;
    }
    if (s_open_pos >= UART_SHELL_OPEN_LEN) {
        s_open_pos = 0U;
        return true;
    }
    return false;
}

/*
 * 口令监听 + 输入泵 + 回收：
 *   未激活时只匹配 "hello"，命中后回复并创建实例；
 *   激活后把收到的字节投喂会话（会话输入任务负责组行与执行）；
 *   实例退出（exit）后回收内存并回到监听。
 */
static void uart_shell_watch_task(void *arg)
{
    static const char reply[] = "hello!\r\n";
    hal_uart_t *uart = (hal_uart_t *)arg;
    uint8_t ch;

    for (;;) {
        bool got = false;

        if (rs_shell_task_finished(s_shell)) {
            /* exit 之后任务已退出：销毁任务并释放实例缓冲 */
            (void)rs_shell_shutdown(s_shell);
            s_shell = NULL;
            s_open_pos = 0U;
            continue;
        }

        /*
         * 非阻塞收字节：有数据时连续收完（UART 只有 1 字节硬件缓冲，
         * 不能每 5ms 只取一个字节，否则会丢字符）；无数据时睡眠让出 CPU。
         */
        while (hal_uart_receive != NULL &&
               hal_uart_receive(uart, &ch, 1U, 0U) == HAL_OK) {
            got = true;
            if (rs_shell_started(s_shell)) {
                rs_shell_feed(s_shell, (const char *)&ch, 1U);
            } else if (uart_shell_open_word_match(ch)) {
                (void)uart_shell_write(reply, sizeof(reply) - 1U, NULL);
                s_shell = rs_shell_start(s_version);
            }
        }

        if (!got) {
            ros_sleep_ms(5U);
        }
    }
}

int rs_platform_start(const char *version)
{
    hal_uart_t *uart;

    if (hal_uart_transmit == NULL) {
        return -1;
    }
    uart = uart_shell_uart();
    s_version = version;

    /* 实例与输入任务等收到 "hello" 才创建 */
    if (s_watch_task == NULL) {
        s_watch_task = ros_task_create_dynamic("sh_watch", UART_SHELL_PRIO,
                                               uart_shell_watch_task, uart,
                                               UART_SHELL_STACK);
    }
    return (s_watch_task != NULL) ? 0 : -1;
}

#endif /* ROS_PLATFORM_STM32 */
