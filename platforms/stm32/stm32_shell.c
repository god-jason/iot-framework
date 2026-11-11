/*
 * stm32_shell.c - STM32 平台 Shell 默认绑定（UART1，按需加载）
 *
 * 内存策略（RAM 紧张的 MCU）：
 *   - 默认只创建一个 768B 栈的监听任务，不分配任何 Shell 缓冲；
 *   - 收到第一笔串口输入才惰性创建实例（约 2.4KB）与输入任务（3KB）；
 *   - SHELL_IDLE_FREE_MS（默认 30s）内无输入，输入任务自行退出，
 *     监听任务随后回收实例缓冲，回到近零占用；
 *   - 有 JOB 正在执行时不回收（由 shell 任务内部判断）。
 *
 * 输出：hal_uart_transmit()（板级通常映射 USART1）
 * 输入：hal_uart_receive() 轮询，收到字节即 shell_feed()
 * 日志：log 的 printf 由工程的 _write 重定向到同一 UART1
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

/*
 * 首字节监听 + 输入泵：
 *   未激活时只等首字节（不占用 shell 缓冲）；
 *   激活后把字节投递给实例，并在实例因空闲超时退出后回收其内存。
 */
static void stm32_shell_watch_task(void *arg)
{
    hal_uart_t *uart = (hal_uart_t *)arg;
    uint8_t ch;

    for (;;) {
        if (!shell_active()) {
            if (hal_uart_receive != NULL &&
                hal_uart_receive(uart, &ch, 1U, 100U) == HAL_OK &&
                shell_lazy_start() == 0) {
                (void)shell_feed((const char *)&ch, 1U);
            }
            continue;
        }

        if (shell_task_finished()) {
            /* 空闲超时后任务已退出：销毁任务并释放实例缓冲 */
            (void)shell_shutdown();
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

    /* 只保存配置；实例与输入任务等第一笔输入到来才创建 */
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
