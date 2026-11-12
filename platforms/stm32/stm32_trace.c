/*
 * stm32_trace.c - MCU 平台 trace 串口流式输出
 *
 * 事件由 ros_trace.c 格式化成一行文本，这里一行一次串口发送。
 * 通过 ros_trace_set_output() 注册已初始化的 hal_uart_t 句柄（指定串口）；
 * 未注册时输出自动丢弃。
 *
 * hal_uart_transmit() 以 weak 方式引用：没有启用 Cube HAL 的工程也能链接
 * （未注册句柄时输出自动丢弃），不会因为 trace 引入额外库依赖。
 */

#if defined(ROS_PLATFORM_STM32)

#include "ros.h"

/* 与 hal_uart.h 中原型 ABI 一致；此处不 include hal 头以免引入平台实现依赖 */
extern int hal_uart_transmit(void *handle, const uint8_t *data,
                             size_t length, uint32_t timeout)
    __attribute__((weak));

static void       *s_uart;
static uint32_t    s_timeout_ms;

void ros_trace_set_output(void *handle, uint32_t timeout_ms)
{
    s_uart = handle;
    s_timeout_ms = timeout_ms;
}

void ros_port_trace_write(const char *data, uint32_t len)
{
    if ((s_uart == NULL) || (data == NULL) || (len == 0U) ||
        (hal_uart_transmit == NULL)) {
        return;
    }
    (void)hal_uart_transmit(s_uart, (const uint8_t *)data,
                            (size_t)len, s_timeout_ms);
}

/* MCU 无文件系统：文件接口为空实现，保持跨平台 API 一致 */
void ros_trace_start_file(const char *path) { (void)path; }
void ros_trace_stop_file(void) {}

#endif /* ROS_PLATFORM_STM32 */
