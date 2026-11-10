/*
 * stm32_shell.c - STM32 平台 Shell 默认绑定（UART1）
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

/* 板级约定：UART1 用非空句柄 1 表示 */
#define STM32_SHELL_UART ((hal_uart_t *)(void *)1)

static void *s_rx_task;

static int stm32_shell_write(const char *data, uint32_t len, void *user)
{
    (void)user;
    if (data == NULL || len == 0U) {
        return 0;
    }
    if (hal_uart_transmit == NULL) {
        return -1;
    }
    return (hal_uart_transmit(STM32_SHELL_UART, (const uint8_t *)data,
                              (size_t)len, 100U) == HAL_OK) ? (int)len : -1;
}

/* 输入泵：每次读 1 字节，超时则让出 CPU */
static void stm32_shell_rx_task(void *arg)
{
    uint8_t ch;

    (void)arg;
    for (;;) {
        if (hal_uart_receive != NULL &&
            hal_uart_receive(STM32_SHELL_UART, &ch, 1U, 20U) == HAL_OK) {
            (void)shell_feed((const char *)&ch, 1U);
        } else {
            iot_sleep_ms(5U);
        }
    }
}

int shell_platform_start(const char *version)
{
    shell_config_t cfg;

    if (hal_uart_transmit == NULL) {
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.version = version;
    cfg.write = stm32_shell_write;
    cfg.user = NULL;
    cfg.reset = (hal_system_reset != NULL) ? hal_system_reset : NULL;

    if (shell_setup(&cfg) != 0) {
        return -1;
    }
    if (shell_start() != 0) {
        return -1;
    }
    if (s_rx_task == NULL) {
        /* 优先级低于 shell 任务：先让 shell 任务打印提示符 */
        s_rx_task = iot_task_create_dynamic("uart_rx", 25U,
                                            stm32_shell_rx_task, NULL, 1024U);
    }
    return (s_rx_task != NULL) ? 0 : -1;
}

#endif /* IOT_PLATFORM_STM32 */
