/* iot-os 综合示例：串口控制台日志 + shell 命令 + LED 闪烁 + 传感器读取。
 * host 下 UART1 输出到 stdout 演示；真实 MCU 替换 iot_port_uart_t 即可。 */
#include "iot_logger.h"
#include "iot_console.h"
#include "iot_shell.h"
#include "iot_mcu.h"
#include "led.h"
#include "voltage.h"

#include <stdio.h>
#include <string.h>

/* ---- 模拟 UART1：输出到 stdout ---- */
static int uart_send(void *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    (void)ctx;
    for (i = 0; i < len; i++) {
        putchar(data[i]);
    }
    return (int)len;
}
static uint32_t uart_avail(void *ctx) { (void)ctx; return 0; }
static int uart_recv(void *ctx, uint8_t *data, uint32_t len)
{ (void)ctx; (void)data; (void)len; return 0; }

/* ---- shell 命令 ---- */
static int cmd_hello(int argc, char **argv)
{ (void)argc; (void)argv; iot_console_puts("hello from iot-os"); return IOT_OK; }

int main(void)
{
    iot_port_uart_t uart;
    iot_console_config_t cfg;
    iot_shell_command_t cmd;
    led_t led;
    uint8_t rx[64];
    int i;

    /* 日志 -> 控制台 -> UART1 */
    iot_log_init();
    memset(&uart, 0, sizeof(uart));
    uart.send = uart_send;
    uart.receive = uart_recv;
    uart.available = uart_avail;
    cfg.uart = &uart;
    cfg.rx_buffer = rx;
    cfg.rx_buffer_size = sizeof(rx);
    iot_console_init(&cfg);
    iot_console_start();   /* log: console ready */

    /* 注册命令 */
    cmd.name = "hello";
    cmd.description = "say hello";
    cmd.callback = cmd_hello;
    iot_shell_register(&cmd);

    /* LED 闪烁 */
    led_init(&led, 0, 1);

    for (i = 0; i < 5; i++) {
        led_toggle(&led);
        LOG_I("demo", "tick %d", i);   /* blink tick */
        iot_console_process();
    }

    iot_shell_execute("hello");

    printf("\ndemo finished\n");
    return 0;
}