/*
 * stm32_io.c - STM32 平台默认 stdio（UART1）的 ros_io 实现
 *
 * 板级可选钩子 board_shell_uart() 返回实际 UART 句柄；未提供时用非空
 * 句柄 1 表示 UART1（与 stm32_shell.c 的约定一致）。
 * hal_uart_* 以 weak 方式引用：未链接 HAL/板级实现时仍可编译，
 * 此时 read/write 返回错误，不影响内核其余部分。
 */

#if defined(ROS_PLATFORM_STM32)

#include "ros.h"
#include "common.h"
#include "hal_uart.h"

extern int hal_uart_transmit(hal_uart_t *handle, const uint8_t *data,
                             size_t length, uint32_t timeout)
    __attribute__((weak));
extern int hal_uart_receive(hal_uart_t *handle, uint8_t *data,
                            size_t length, uint32_t timeout)
    __attribute__((weak));
extern hal_uart_t *board_shell_uart(void) __attribute__((weak));

#define STM32_IO_UART_DEFAULT ((hal_uart_t *)(void *)1)
#define STM32_IO_TIMEOUT_MS   100U

static hal_uart_t *stm32_io_uart(void)
{
    if (board_shell_uart != NULL) {
        hal_uart_t *handle = board_shell_uart();

        if (handle != NULL) {
            return handle;
        }
    }
    return STM32_IO_UART_DEFAULT;
}

static int stm32_io_read(ros_io_t *io, void *buf, uint32_t len)
{
    (void)io;
    if (buf == NULL || len == 0U) {
        return -1;
    }
    if (hal_uart_receive == NULL) {
        return -1;
    }
    if (hal_uart_receive(stm32_io_uart(), (uint8_t *)buf, (size_t)len,
                         STM32_IO_TIMEOUT_MS) != HAL_OK) {
        return 0;               /* 超时：当前暂无数据 */
    }
    return (int)len;
}

static int stm32_io_write(ros_io_t *io, const void *buf, uint32_t len)
{
    (void)io;
    if (buf == NULL) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }
    if (hal_uart_transmit == NULL ||
        hal_uart_transmit(stm32_io_uart(), (const uint8_t *)buf, (size_t)len,
                          STM32_IO_TIMEOUT_MS) != HAL_OK) {
        return -1;
    }
    return (int)len;
}

static int stm32_io_close(ros_io_t *io)
{
    (void)io;
    return 0;
}

ros_io_t *ros_io_platform_default(void)
{
    static ros_io_t io;

    io.read = stm32_io_read;
    io.write = stm32_io_write;
    io.close = stm32_io_close;
    io.user = NULL;
    return &io;
}

/* ros_init() 调用：把 UART1 注册为默认 stdio */
void ros_io_platform_init(void)
{
    ros_io_set_default(ros_io_platform_default());
}

#endif /* ROS_PLATFORM_STM32 */
