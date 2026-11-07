/* iot-os 综合示例：串口控制台日志 + shell 命令 + LED 闪烁 + 传感器读取。
 * host 下 UART1 输出到 stdout 演示；真实 MCU 替换 port_uart_handle_t 即可。 */
#include "iot_logger.h"
#include "iot_console.h"
#include "port.h"
#include "led.h"
#include "voltage.h"

#include <stdio.h>
#include <string.h>

/* ---- shell 命令 ---- */
static int cmd_hello(int argc, char **argv)
{ (void)argc; (void)argv; iot_console_puts("hello from iot-os"); return IOT_OK; }

int main(void)
{
    port_uart_handle_t uart;
    port_uart_config_t uart_cfg;
    iot_console_config_t cfg;
    iot_shell_command_t cmd;
    led_t led;
    uint8_t rx[64];
    int i;

    /* 日志 -> 控制台 -> UART1 */
    iot_log_init();
    memset(&uart, 0, sizeof(uart));
    memset(&uart_cfg, 0, sizeof(uart_cfg));
    uart.base.instance = (void *)1;   /* USART1 */
    uart_cfg.baudrate = 115200;
    uart_cfg.word_length = PORT_UART_WORD_8BIT;
    uart_cfg.parity = PORT_UART_PARITY_NONE;
    uart_cfg.stop_bits = PORT_UART_STOP_1;
    uart_cfg.flow_control = PORT_UART_FLOW_NONE;
    uart_cfg.mode = PORT_UART_MODE_TX_RX;
    uart_cfg.oversampling = false;
    port_uart_init(&uart, &uart_cfg);

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
