/* iot-os 综合示例：控制台日志 + shell 命令 + LED 闪烁 + 输入处理。
 * 模拟器下控制台使用标准输入/输出；STM32 下同一套 console API 走 UART1。 */
#include "log.h"
#include "console.h"
#include "iot.h"
#include "led.h"

#include <stdio.h>
#include <string.h>

/* 收到 exit 命令后提前结束演示 */
static volatile int s_exit;

/* ---- shell 命令 ---- */
static int cmd_hello(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_puts("hello from iot-os");
    return 0;
}

static int cmd_exit(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    s_exit = 1;
    console_puts("bye");
    return 0;
}

int main(void)
{
    iot_uart_t uart;
    iot_uart_config_t uart_cfg;
    console_config_t cfg;
    shell_command_t cmd;
    led_t led;
    uint8_t rx[64];
    int i;
    int j;

    /* PC 平台：instance == (void *)1 约定为 UART1，收发映射到 stdin/stdout；
     * STM32 上填写真实 UART1 句柄即可复用同一套控制台代码。 */
    memset(&uart, 0, sizeof(uart));
    memset(&uart_cfg, 0, sizeof(uart_cfg));
    uart.base.instance = (void *)1;
    uart_cfg.baudrate = 115200;
    uart_cfg.word_length = IOT_UART_WORD_8BIT;
    uart_cfg.parity = IOT_UART_PARITY_NONE;
    uart_cfg.stop_bits = IOT_UART_STOP_1;
    uart_cfg.flow_control = IOT_UART_FLOW_NONE;
    uart_cfg.mode = IOT_UART_MODE_TX_RX;
    uart_cfg.oversampling = false;
    if (iot_uart_init(&uart, &uart_cfg) != IOT_OK) {
        printf("uart init failed\n");
        return 1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &uart;
    cfg.rx_buffer = rx;
    cfg.rx_buffer_size = sizeof(rx);
    if (console_init(&cfg) != 0) {
        printf("console init failed\n");
        return 1;
    }
    console_start();   /* 输出就绪提示 */

    /* 注册命令 */
    cmd.name = "hello";
    cmd.description = "say hello";
    cmd.callback = cmd_hello;
    shell_register(&cmd);

    cmd.name = "exit";
    cmd.description = "stop demo";
    cmd.callback = cmd_exit;
    shell_register(&cmd);

    /* LED 闪烁：每 100ms 处理一次控制台输入，5 秒为一个大周期 */
    led_init(&led, 0, 1);

    for (i = 0; i < 5 && !s_exit; i++) {
        led_toggle(&led);
        LOG_I("demo", "tick %d (输入 hello / exit)", i);
        for (j = 0; j < 50 && !s_exit; j++) {
            console_process();
            iot_delay_ms(100);
        }
    }

    shell_execute("hello");

    printf("\ndemo finished\n");
    return 0;
}
