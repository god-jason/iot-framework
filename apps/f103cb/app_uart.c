/*
 * app_uart.c - STM32F103CBT6 应用串口初始化
 *
 * 通过平台 HAL（platforms/stm32f1/hal_uart.c）初始化两路串口：
 *   UART1(PA9/PA10)：日志 + roshell；
 *   UART2(PA2/PA3) ：ML307 4G 模组。
 *
 * 板级不再初始化串口；本文件同时提供 board_shell_uart() 与
 * board_trace_start() 两个板级钩子，分别用于 stdout/shell 与 trace。
 */

#include "app_uart.h"

#include "ros.h"
#include "hal_uart.h"

#include <string.h>

static hal_uart_t s_console;
static hal_uart_t s_ml307;

/* instance：0 = USART1，1 = USART2（平台内部按编号定位实例） */
static void app_uart_setup(hal_uart_t *handle, uint8_t instance)
{
    hal_uart_config_t config;

    memset(&config, 0, sizeof(config));
    config.baudrate = 115200U;
    config.word_length = HAL_UART_WORD_8BIT;
    config.parity = HAL_UART_PARITY_NONE;
    config.stop_bits = HAL_UART_STOP_1;
    config.flow_control = HAL_UART_FLOW_NONE;
    config.mode = HAL_UART_MODE_TX_RX;
    config.oversampling = false;
    config.id = instance;

    (void)hal_uart_init(handle, &config);
}

void app_uart_init(void)
{
    app_uart_setup(&s_console, 0U);
    app_uart_setup(&s_ml307, 1U);

    ros_trace_set_output(&s_console, 0U);
    ros_trace_set_level(ROS_TRACE_LEVEL_CORE);
}

hal_uart_t *app_uart_console(void)
{
    return &s_console;
}

hal_uart_t *app_uart_ml307(void)
{
    return &s_ml307;
}

/* 平台约定：stdout / roshell 使用 board_shell_uart() 返回的句柄 */
hal_uart_t *board_shell_uart(void)
{
    return &s_console;
}

/*
 * 平台 main 在调度器启动前调用，此时 UART 尚未初始化：
 * 仅注册 trace 输出句柄，初始化完成后日志自动生效。
 */
void board_trace_start(uint32_t baud)
{
    (void)baud;

    ros_trace_set_output(&s_console, 0U);
    ros_trace_set_level(ROS_TRACE_LEVEL_CORE);
}
