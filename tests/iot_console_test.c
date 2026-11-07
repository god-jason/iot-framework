/*
 * iot_console 单元测试。
 * 说明：使用模拟平台 UART 验证发送/接收路径。
 */
#include "test_framework.h"
#include "iot_console.h"
#include "iot_logger.h"
#include "port.h"
#include "port_sim.h"

#include <string.h>

#define TX_MAX 2048

static port_uart_handle_t s_uart;
static uint8_t s_tx[TX_MAX + 1];
static uint32_t s_tx_len;

static uint8_t s_rx[64];

static void uart_fixture_reset(void)
{
    port_uart_config_t cfg;

    port_sim_reset_state();
    memset(&s_uart, 0, sizeof(s_uart));
    memset(&cfg, 0, sizeof(cfg));
    cfg.baudrate = 115200;
    cfg.word_length = PORT_UART_WORD_8BIT;
    cfg.parity = PORT_UART_PARITY_NONE;
    cfg.stop_bits = PORT_UART_STOP_1;
    cfg.flow_control = PORT_UART_FLOW_NONE;
    cfg.mode = PORT_UART_MODE_TX_RX;
    (void)port_uart_init(&s_uart, &cfg);
    s_tx_len = 0;
    s_tx[0] = '\0';
}

static void refresh_tx(void)
{
    s_tx_len = port_sim_uart_get_tx(&s_uart, s_tx, TX_MAX);
    s_tx[s_tx_len] = '\0';
}

static void set_rx(const char *s)
{
    (void)port_sim_uart_inject(&s_uart, (const uint8_t *)s, (uint32_t)strlen(s));
}

static void test_before_init(void)
{
    tf_begin("console: 未初始化时 puts 返回 -1");
    TF_CHECK_INT(iot_console_puts("x"), -1);
    tf_end();
}

static void test_init_errors(void)
{
    iot_console_config_t cfg;

    tf_begin("console: init 非法参数");
    TF_CHECK_INT(iot_console_init(NULL), -1);

    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = NULL;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(iot_console_init(&cfg), -1);
    tf_end();
}

static void test_puts_and_start(void)
{
    iot_console_config_t cfg;

    tf_begin("console: init 成功后 puts 输出一行 CRLF");
    uart_fixture_reset();
    iot_log_init();
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(iot_console_init(&cfg), IOT_OK);

    TF_CHECK_INT(iot_console_puts("hello"), IOT_OK);
    refresh_tx();
    TF_CHECK_STR((const char *)s_tx, "hello\r\n");
    TF_CHECK_INT(s_tx_len, 7);
    TF_CHECK_INT(iot_console_puts(NULL), -1);
    tf_end();

    tf_begin("console: start 输出就绪提示");
    uart_fixture_reset();
    TF_CHECK_INT(iot_console_start(), IOT_OK);
    refresh_tx();
    TF_CHECK_STR((const char *)s_tx, "iot-os console ready\r\n");
    tf_end();
}

static void test_process(void)
{
    iot_console_config_t cfg;
    static const char input[] = "help\n";

    tf_begin("console: process 无输入时返回成功");
    uart_fixture_reset();
    iot_log_init();
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(iot_console_init(&cfg), IOT_OK);
    TF_CHECK_INT(iot_console_process(), IOT_OK);
    TF_CHECK_INT((int)port_uart_get_rx_count(&s_uart), 0);
    tf_end();

    tf_begin("console: process 读取并消费 UART 数据");
    uart_fixture_reset();
    iot_log_init();
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(iot_console_init(&cfg), IOT_OK);

    memset(s_rx, 0, sizeof(s_rx));
    set_rx(input);
    TF_CHECK_INT(iot_console_process(), IOT_OK);
    TF_CHECK_MEM(s_rx, input, sizeof(input) - 1);
    TF_CHECK_INT((int)port_uart_get_rx_count(&s_uart), 0);
    tf_end();
}

static void test_logger_backend(void)
{
    iot_console_config_t cfg;
    uint32_t before;

    tf_begin("console: 注册 logger 后端输出到 UART");
    uart_fixture_reset();
    iot_log_init();
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(iot_console_init(&cfg), IOT_OK);
    TF_CHECK_INT(iot_log_set_level(IOT_LOG_DEBUG), IOT_OK);

    iot_log_write(IOT_LOG_INFO, "T", "msg");
    refresh_tx();
    TF_CHECK(strstr((const char *)s_tx, "[I][T] msg\r\n") != NULL);

    before = s_tx_len;
    TF_CHECK_INT(iot_log_set_level(IOT_LOG_ERROR), IOT_OK);
    iot_log_write(IOT_LOG_INFO, "T", "hidden");
    refresh_tx();
    TF_CHECK_INT(s_tx_len, before);

    iot_log_write(IOT_LOG_ERROR, NULL, "err");
    refresh_tx();
    TF_CHECK(s_tx_len > before);
    TF_CHECK(strstr((const char *)s_tx, "[E][-] err\r\n") != NULL);
    tf_end();
}

static void test_uart_not_initialized(void)
{
    iot_console_config_t cfg;

    tf_begin("console: UART 未初始化时不发送数据");
    port_sim_reset_state();
    memset(&s_uart, 0, sizeof(s_uart));    /* 不调用 port_uart_init */
    s_tx_len = 0;
    s_tx[0] = '\0';
    iot_log_init();
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx;
    cfg.rx_buffer_size = sizeof(s_rx);
    TF_CHECK_INT(iot_console_init(&cfg), IOT_OK);

    TF_CHECK_INT(iot_console_puts("x"), IOT_OK);
    refresh_tx();
    TF_CHECK_INT(s_tx_len, 0);
    TF_CHECK_INT(iot_console_process(), IOT_OK);
    TF_CHECK_INT((int)port_uart_get_rx_count(&s_uart), 0);
    tf_end();
}

void ut_iot_console(void)
{
    test_before_init();
    test_init_errors();
    test_puts_and_start();
    test_process();
    test_logger_backend();
    test_uart_not_initialized();
}
