/*
 * stm32_shell.c - STM32 平台 Shell 默认绑定（UART1，口令开启）
 *
 * 内存策略（RAM 紧张的 MCU）：
 *   - 上电只创建一个 768B 栈的监听任务，不分配任何 Shell 缓冲；
 *   - 串口收到口令 "hello"（连续 5 个字符，不需要回车）时回复 "hello!"，
 *     并惰性创建 Shell 实例（约 1.4KB）与输入任务（3KB）；
 *   - Shell 激活后常驻，不会因为长时间无操作自动关闭；
 *   - 输入 exit 命令（按需实例内置）：打印 bye，输入任务退出，
 *     监听任务回收实例内存，重新回到只监听口令的状态。
 *
 * 日志不受影响：log 的 printf 由工程的 _write 直接走 hal_uart_transmit，
 * trace 也由 board_trace_start() 独立绑定，与 Shell 实例是否存在无关。
 *
 * 输出：hal_uart_transmit()（板级通常映射 USART1）
 * 输入：hal_uart_receive() 轮询，收到字节即 shell_feed()
 *
 * hal_uart_* 以 weak 方式引用：未链接 HAL/板级实现时仍可编译，
 * 此时 shell_platform_start() 返回 -1，不影响内核其余部分。
 */

#if defined(IOT_PLATFORM_STM32)

#include "shell.h"
#include "iot.h"
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
extern void hal_system_reset(void) __attribute__((weak));

/* 板级约定：未提供 board_shell_uart() 时用非空句柄 1 表示 UART1 */
#define STM32_SHELL_UART_DEFAULT ((hal_uart_t *)(void *)1)

/* 板级可选钩子：返回实际使用的 UART 句柄（Cube HAL 下必须是有效句柄） */
extern hal_uart_t *board_shell_uart(void) __attribute__((weak));

#define STM32_OPEN_WORD "hello"
#define STM32_OPEN_LEN  5U

static hal_uart_t *stm32_shell_uart(void)
{
    if (board_shell_uart != NULL) {
        hal_uart_t *handle = board_shell_uart();

        if (handle != NULL) {
            return handle;
        }
    }
    return STM32_SHELL_UART_DEFAULT;
}

static void *s_watch_task;
static size_t s_open_pos;               /* 口令匹配进度 */

static int stm32_shell_write(const char *data, uint32_t len, void *user)
{
    (void)user;
    if (data == NULL || len == 0U) {
        return 0;
    }
    if (hal_uart_transmit == NULL) {
        return -1;
    }
    return (hal_uart_transmit(stm32_shell_uart(), (const uint8_t *)data,
                              (size_t)len, 100U) == HAL_OK) ? (int)len : -1;
}

/* 逐字节匹配开启口令；匹配过程中其它字符被丢弃 */
static bool stm32_open_word_match(uint8_t ch)
{
    const char *word = STM32_OPEN_WORD;

    if (ch == (uint8_t)word[s_open_pos]) {
        s_open_pos++;
    } else {
        s_open_pos = (ch == (uint8_t)word[0]) ? 1U : 0U;
    }
    if (s_open_pos >= STM32_OPEN_LEN) {
        s_open_pos = 0U;
        return true;
    }
    return false;
}

/*
 * 口令监听 + 输入泵：
 *   未激活时只匹配 "hello"，命中后回复并创建实例；
 *   激活后把字节投递给实例，实例退出（exit）后回收内存并回到监听。
 */
static void stm32_shell_watch_task(void *arg)
{
    static const char reply[] = "hello!\r\n";
    hal_uart_t *uart = (hal_uart_t *)arg;
    uint8_t ch;

    for (;;) {
        if (!shell_started()) {
            if (hal_uart_receive != NULL &&
                hal_uart_receive(uart, &ch, 1U, 100U) == HAL_OK &&
                stm32_open_word_match(ch)) {
                (void)stm32_shell_write(reply, sizeof(reply) - 1U, NULL);
                (void)shell_lazy_start();
            }
            continue;
        }

        if (shell_task_finished()) {
            /* exit 之后任务已退出：销毁任务并释放实例缓冲 */
            (void)shell_shutdown();
            s_open_pos = 0U;
            continue;
        }

        if (!shell_active()) {
            /* 任务已创建，等它自己申请好实例缓冲再喂字节 */
            iot_sleep_ms(5U);
            continue;
        }

        if (hal_uart_receive != NULL &&
            hal_uart_receive(uart, &ch, 1U, 100U) == HAL_OK) {
            (void)shell_feed((const char *)&ch, 1U);
        }
    }
}

int shell_platform_start(const char *version)
{
    shell_config_t cfg;
    hal_uart_t *uart;

    if (hal_uart_transmit == NULL) {
        return -1;
    }
    uart = stm32_shell_uart();

    memset(&cfg, 0, sizeof(cfg));
    cfg.version = version;
    cfg.write = stm32_shell_write;
    cfg.user = NULL;
    cfg.reset = (hal_system_reset != NULL) ? hal_system_reset : NULL;

    /* 只保存配置；实例与输入任务等收到 "hello" 才创建 */
    if (shell_lazy_setup(&cfg) != 0) {
        return -1;
    }
    if (s_watch_task == NULL) {
        s_watch_task = iot_task_create_dynamic("sh_watch",
                                               (uint8_t)SHELL_WATCH_PRIO,
                                               stm32_shell_watch_task, uart,
                                               (uint32_t)SHELL_WATCH_STACK);
    }
    return (s_watch_task != NULL) ? 0 : -1;
}

#endif /* IOT_PLATFORM_STM32 */
