/*
 * console 单元测试。
 * 说明：使用模拟平台 UART 验证发送/接收路径。
 */
#include "test.h"
#include "console.h"
#include "log.h"
#include "iot.h"
#include "pc.h"

#include <string.h>

#define TX_MAX 2048

static iot_uart_t s_uart;
static uint8_t s_tx[TX_MAX + 1];
static uint32_t s_tx_len;

static uint8_t s_rx[64];

static void uart_fixture_reset(void)
{
    iot_uart_config_t cfg;

    iot_test_reset_state();
    memset(&s_uart, 0, sizeof(s_uart));
    memset(&cfg, 0, sizeof(cfg));
    cfg.baudrate = 115200;
    cfg.word_length = IOT_UART_WORD_8BIT;
    cfg.parity = IOT_UART_PARITY_NONE;
    cfg.stop_bits = IOT_UART_STOP_1;
    cfg.flow_control = IOT_UART_FLOW_NONE;
    cfg.mode = IOT_UART_MODE_TX_RX;
    (void)iot_uart_init(&s_uart, &cfg);
    s_tx_len = 0;
    s_tx[0] = '\0';
}

static void refresh_tx(void)
{
    s_tx_len = iot_test_uart_get_tx(&s_uart, s_tx, TX_MAX);
    s_tx[s_tx_len] = '\0';
}

static void set_rx(const char *s)
{
    (void)iot_test_uart_inject(&s_uart, (const uint8_t *)s, (uint32_t)strlen(s));
}

static void test_before_init(void)
{
    tf_begin("console: 未初始化时 puts 返回 -1");
    TF_CHECK_INT(console_puts("x"), -1);
    tf_end();
}

static void test_init_errors(void)
{
    console_config_t cfg;

    tf_begin("console: init 非法参数");
    TF_CHECK_INT(console_init(NULL), -1);

    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = NULL;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(console_init(&cfg), -1);
    tf_end();
}

static void test_puts_and_start(void)
{
    console_config_t cfg;

    tf_begin("console: init 成功后 puts 输出一行 CRLF");
    uart_fixture_reset();
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(console_init(&cfg), 0);

    TF_CHECK_INT(console_puts("hello"), 0);
    refresh_tx();
    TF_CHECK_STR((const char *)s_tx, "hello\r\n");
    TF_CHECK_INT(s_tx_len, 7);
    TF_CHECK_INT(console_puts(NULL), -1);
    tf_end();

    tf_begin("console: start 输出就绪提示");
    uart_fixture_reset();
    TF_CHECK_INT(console_start(), 0);
    refresh_tx();
    TF_CHECK_STR((const char *)s_tx, "iot-os console ready\r\n");
    tf_end();
}

static void test_process(void)
{
    console_config_t cfg;
    static const char input[] = "help\n";

    tf_begin("console: process 无输入时返回成功");
    uart_fixture_reset();
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(console_init(&cfg), 0);
    TF_CHECK_INT(console_process(), 0);
    TF_CHECK_INT((int)iot_uart_get_rx_count(&s_uart), 0);
    tf_end();

    tf_begin("console: process 读取并消费 UART 数据");
    uart_fixture_reset();
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(console_init(&cfg), 0);

    memset(s_rx, 0, sizeof(s_rx));
    set_rx(input);
    TF_CHECK_INT(console_process(), 0);
    TF_CHECK_MEM(s_rx, input, sizeof(input) - 1);
    TF_CHECK_INT((int)iot_uart_get_rx_count(&s_uart), 0);
    tf_end();
}

static void test_uart_not_initialized(void)
{
    console_config_t cfg;

    tf_begin("console: UART 未初始化时不发送数据");
    iot_test_reset_state();
    memset(&s_uart, 0, sizeof(s_uart));    /* 不调用 iot_uart_init */
    s_tx_len = 0;
    s_tx[0] = '\0';
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(console_init(&cfg), 0);

    TF_CHECK_INT(console_puts("x"), 0);
    refresh_tx();
    TF_CHECK_INT(s_tx_len, 0);
    TF_CHECK_INT(console_process(), 0);
    TF_CHECK_INT((int)iot_uart_get_rx_count(&s_uart), 0);
    tf_end();
}

void ut_console(void)
{
    test_before_init();
    test_init_errors();
    test_puts_and_start();
    test_process();
    test_uart_not_initialized();
}
