/* components/modem/ml307/at.c per-file 单元测试。
 *
 * 覆盖：初始化/启停/重复 deinit 的幂等性、发送与原始发送参数校验、
 *       非阻塞事务（成功/失败/取消/超时/重试/提示符）、
 *       数据行 expect 过滤、URC 优先分发、响应缓冲截断、
 *       行缓冲溢出、UART 接收泵取与未启动时不泵取。
 * 未覆盖：真实模组响应时序；at_process 的 tick 使用内部计数而非真实时间。
 */
#include "test.h"
#include "at.h"
#include "at_urc.h"
#include "hal.h"
#include "pc.h"

#include <string.h>

#define TX_MAX 2048
#define RX_MAX 2048

static hal_uart_t s_uart;

static uint8_t s_tx[TX_MAX + 1];
static uint32_t s_tx_len;

static char s_line_buf[512];
static uint8_t s_rx_buf[64];

static int s_cb_calls;
static at_result_e s_cb_result;
static char s_cb_resp[512];
static uint32_t s_cb_len;
static void *s_cb_user;

static int s_urc_calls;

static void uart_fixture_reset(void)
{
    hal_uart_config_t cfg;

    iot_test_reset_state();
    memset(&s_uart, 0, sizeof(s_uart));
    memset(&cfg, 0, sizeof(cfg));
    cfg.baudrate = 115200;
    cfg.word_length = HAL_UART_WORD_8BIT;
    cfg.parity = HAL_UART_PARITY_NONE;
    cfg.stop_bits = HAL_UART_STOP_1;
    cfg.flow_control = HAL_UART_FLOW_NONE;
    cfg.mode = HAL_UART_MODE_TX_RX;
    (void)hal_uart_init(&s_uart, &cfg);
    s_tx_len = 0;
    s_tx[0] = '\0';
}

static void reset_mock(void)
{
    uart_fixture_reset();
}

static void set_rx(const char *s)
{
    (void)iot_test_uart_inject(&s_uart, (const uint8_t *)s, (uint32_t)strlen(s));
}

static void refresh_tx(void)
{
    s_tx_len = iot_test_uart_get_tx(&s_uart, s_tx, TX_MAX);
    s_tx[s_tx_len] = '\0';
}

static void reset_cb(void)
{
    s_cb_calls = 0;
    s_cb_result = AT_RESULT_IO_ERROR;
    s_cb_resp[0] = '\0';
    s_cb_len = 0;
    s_cb_user = NULL;
}

static void on_done(at_result_e result, const char *response, uint32_t len,
                    void *user)
{
    uint32_t n = len;

    s_cb_calls++;
    s_cb_result = result;
    s_cb_user = user;
    s_cb_len = len;
    if (n > sizeof(s_cb_resp) - 1) {
        n = sizeof(s_cb_resp) - 1;
    }
    if (response != NULL) {
        memcpy(s_cb_resp, response, n);
    }
    s_cb_resp[n] = '\0';
}

static void urc_on_ring(const char *line, uint32_t len, void *user)
{
    (void)line;
    (void)len;
    (void)user;
    s_urc_calls++;
}

static int init_at(uint32_t default_timeout, uint8_t default_retry)
{
    at_config_t cfg;
    int r;

    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx_buf;
    cfg.rx_buffer_size = sizeof(s_rx_buf);
    cfg.line_buffer = s_line_buf;
    cfg.line_buffer_size = sizeof(s_line_buf);
    cfg.default_timeout = default_timeout;
    cfg.default_retry = default_retry;

    reset_mock();
    reset_cb();
    s_urc_calls = 0;
    r = at_init(&cfg);
    return r;
}

static void make_req(at_request_t *req, const char *cmd, const char *success,
                     const char *failure, uint32_t timeout, uint8_t retry)
{
    memset(req, 0, sizeof(*req));
    req->command = cmd;
    req->success = success;
    req->failure = failure;
    req->timeout = timeout;
    req->retry = retry;
    req->callback = on_done;
    req->user = (void *)0x5A5A;
}

static void test_lifecycle(void)
{
    tf_begin("at/未初始化时所有操作被拒绝");
    at_deinit();
    TF_CHECK_INT(at_start(), -1);
    TF_CHECK_INT(at_send("AT"), -1);
    TF_CHECK_INT(at_send_raw((const uint8_t *)"x", 1), -1);
    {
        at_request_t req;
        make_req(&req, "AT", NULL, NULL, 10, 0);
        TF_CHECK_INT(at_query(&req), -1);
        TF_CHECK_INT(at_exec("AT", 5), AT_RESULT_BUSY);
    }
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK(!at_is_busy());
    TF_CHECK_INT(at_stop(), 0);
    tf_end();

    tf_begin("at/初始化参数校验");
    TF_CHECK_INT(at_init(NULL), -1);
    {
        at_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        TF_CHECK_INT(at_init(&cfg), -1);   /* uart == NULL */
        cfg.uart = &s_uart;
        cfg.line_buffer = s_line_buf;
        cfg.line_buffer_size = sizeof(s_line_buf);
        TF_CHECK_INT(at_init(&cfg), 0);
    }
    TF_CHECK_INT(at_get_state(), AT_IDLE);
    TF_CHECK(!at_is_busy());
    tf_end();

    tf_begin("at/重复初始化与启停幂等");
    TF_CHECK_INT(init_at(5, 0), 0);
    TF_CHECK_INT(init_at(5, 0), 0);
    TF_CHECK_INT(at_start(), 0);
    TF_CHECK_INT(at_start(), 0);
    TF_CHECK_INT(at_stop(), 0);
    TF_CHECK_INT(at_get_state(), AT_IDLE);
    TF_CHECK_INT(at_deinit(), 0);
    TF_CHECK_INT(at_deinit(), 0);
    TF_CHECK_INT(at_send("AT"), -1);
    tf_end();
}

static void test_send(void)
{
    init_at(5, 0);

    tf_begin("at/at_send 追加 CRLF");
    TF_CHECK_INT(at_send("AT"), 0);
    refresh_tx();
    TF_CHECK_STR((const char *)s_tx, "AT\r\n");
    tf_end();

    tf_begin("at/at_send 非法参数不发送");
    reset_mock();
    TF_CHECK_INT(at_send(NULL), -1);
    refresh_tx();
    TF_CHECK_INT(s_tx_len, 0);
    tf_end();

    tf_begin("at/at_send_raw 原样发送");
    reset_mock();
    TF_CHECK_INT(at_send_raw((const uint8_t *)"ABC\r\n", 5), 0);
    refresh_tx();
    TF_CHECK_MEM(s_tx, "ABC\r\n", 5);
    TF_CHECK_INT(s_tx_len, 5);
    TF_CHECK_INT(at_send_raw(NULL, 3), -1);
    TF_CHECK_INT(at_send_raw((const uint8_t *)"x", 0), -1);
    refresh_tx();
    TF_CHECK_INT(s_tx_len, 5);
    tf_end();
}

static void test_query_complete(void)
{
    at_request_t req;

    init_at(5, 0);

    tf_begin("at/提交事务并成功完成");
    make_req(&req, "AT+X", NULL, NULL, 0, 0);
    TF_CHECK_INT(at_query(&req), 0);
    refresh_tx();
    TF_CHECK_STR((const char *)s_tx, "AT+X\r\n");
    TF_CHECK(at_is_busy());
    TF_CHECK_INT(at_get_state(), AT_WAITING);

    make_req(&req, "AT+Y", NULL, NULL, 0, 0);
    TF_CHECK_INT(at_query(&req), -1);      /* 已有在途事务 */

    at_input((const uint8_t *)"line1\r\nline2\r\nOK\r\n", 18);
    TF_CHECK(!at_is_busy());
    TF_CHECK_INT(at_get_state(), AT_COMPLETE);
    TF_CHECK_INT(s_cb_calls, 1);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OK);
    TF_CHECK_INT(s_cb_len, 11);
    TF_CHECK_STR(s_cb_resp, "line1\nline2");
    TF_CHECK_STR(at_get_response(), "line1\nline2");
    TF_CHECK(s_cb_user == (void *)0x5A5A);
    tf_end();

    tf_begin("at/失败终止符");
    init_at(5, 0);
    make_req(&req, "AT", "DONE", "FAIL", 0, 0);
    TF_CHECK_INT(at_query(&req), 0);
    at_input((const uint8_t *)"FAIL\r\n", 6);
    TF_CHECK_INT(s_cb_calls, 1);
    TF_CHECK_INT(s_cb_result, AT_RESULT_ERROR);
    TF_CHECK_INT(at_get_state(), AT_ERROR);
    tf_end();

    tf_begin("at/自定义成功终止符");
    init_at(5, 0);
    make_req(&req, "AT", "DONE", "FAIL", 0, 0);
    TF_CHECK_INT(at_query(&req), 0);
    at_input((const uint8_t *)"data\r\nDONE\r\n", 12);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OK);
    TF_CHECK_STR(at_get_response(), "data");
    tf_end();

    tf_begin("at/expect 数据行过滤");
    init_at(5, 0);
    make_req(&req, "AT", NULL, NULL, 0, 0);
    req.expect = "DATA";
    TF_CHECK_INT(at_query(&req), 0);
    at_input((const uint8_t *)"noise\r\nDATA=1\r\nOK\r\n", 19);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OK);
    TF_CHECK_STR(at_get_response(), "DATA=1");
    tf_end();

    tf_begin("at/无在途事务时忽略噪声");
    init_at(5, 0);
    at_input((const uint8_t *)"garbage\r\nOK\r\n", 13);
    TF_CHECK_INT(s_cb_calls, 0);
    TF_CHECK(!at_is_busy());
    tf_end();

    tf_begin("at/空命令仅等待响应");
    init_at(5, 0);
    make_req(&req, NULL, NULL, NULL, 0, 0);
    TF_CHECK_INT(at_query(&req), 0);
    refresh_tx();
    TF_CHECK_INT(s_tx_len, 0);
    at_input((const uint8_t *)"OK\r\n", 4);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OK);
    tf_end();
}

static void test_cancel(void)
{
    at_request_t req;

    init_at(5, 0);

    tf_begin("at/无事务时取消失败");
    TF_CHECK_INT(at_cancel(), -1);
    tf_end();

    tf_begin("at/取消在途事务");
    make_req(&req, "AT+X", NULL, NULL, 100, 0);
    TF_CHECK_INT(at_query(&req), 0);
    TF_CHECK_INT(at_cancel(), 0);
    TF_CHECK_INT(s_cb_calls, 1);
    TF_CHECK_INT(s_cb_result, AT_RESULT_CANCELLED);
    TF_CHECK(!at_is_busy());
    TF_CHECK_INT(at_get_state(), AT_ERROR);
    tf_end();
}

static void test_prompt(void)
{
    at_request_t req;

    init_at(5, 0);

    tf_begin("at/等待 > 提示符");
    make_req(&req, "AT+PUB", ">", "ERROR", 100, 0);
    TF_CHECK_INT(at_query(&req), 0);
    at_input((const uint8_t *)">", 1);
    TF_CHECK_INT(s_cb_calls, 1);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OK);
    TF_CHECK(!at_is_busy());
    tf_end();

    tf_begin("at/未等待提示符时忽略 >");
    init_at(5, 0);
    make_req(&req, "AT", NULL, NULL, 100, 0);
    TF_CHECK_INT(at_query(&req), 0);
    at_input((const uint8_t *)">", 1);
    TF_CHECK_INT(s_cb_calls, 0);
    TF_CHECK(at_is_busy());
    tf_end();
}

static void test_urc_priority(void)
{
    at_request_t req;

    init_at(5, 0);
    (void)at_urc_unregister("RING");
    TF_CHECK_INT(at_urc_register("RING", urc_on_ring, NULL), 0);

    tf_begin("at/URC 优先分发且不进入响应");
    make_req(&req, "AT", NULL, NULL, 100, 0);
    TF_CHECK_INT(at_query(&req), 0);
    at_input((const uint8_t *)"RING\r\nOK\r\n", 10);
    TF_CHECK_INT(s_urc_calls, 1);
    TF_CHECK_INT(s_cb_calls, 1);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OK);
    TF_CHECK_INT(s_cb_len, 0);
    tf_end();

    (void)at_urc_unregister("RING");
}

static void test_timeout_retry(void)
{
    at_request_t req;
    int i;

    tf_begin("at/超时完成事务");
    init_at(5, 0);
    make_req(&req, "AT+T", NULL, NULL, 4, 0);
    TF_CHECK_INT(at_query(&req), 0);
    for (i = 0; i < 3; i++) {
        TF_CHECK_INT(at_process(), 0);
        TF_CHECK(at_is_busy());
    }
    TF_CHECK_INT(s_cb_calls, 0);
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK_INT(s_cb_calls, 1);
    TF_CHECK_INT(s_cb_result, AT_RESULT_TIMEOUT);
    TF_CHECK(!at_is_busy());
    tf_end();

    tf_begin("at/超时重试后重发命令");
    init_at(5, 0);
    make_req(&req, "AT+R", NULL, NULL, 2, 1);
    TF_CHECK_INT(at_query(&req), 0);
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK_INT(at_process(), 0);      /* 第一次超时 -> 重发 */
    refresh_tx();
    TF_CHECK_STR((const char *)s_tx, "AT+R\r\nAT+R\r\n");
    TF_CHECK(at_is_busy());
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK_INT(at_process(), 0);      /* 重试耗尽 -> 超时 */
    TF_CHECK_INT(s_cb_result, AT_RESULT_TIMEOUT);
    TF_CHECK(!at_is_busy());
    tf_end();

    tf_begin("at/使用默认超时");
    init_at(3, 0);
    make_req(&req, "AT+D", NULL, NULL, 0, 0);
    TF_CHECK_INT(at_query(&req), 0);
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK(at_is_busy());
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK_INT(s_cb_result, AT_RESULT_TIMEOUT);
    tf_end();
}

static void test_buffer_limits(void)
{
    at_config_t cfg;
    at_request_t req;
    static char big[301];
    static char small_line[16];

    tf_begin("at/行缓冲溢出产生 OVERFLOW");
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx_buf;
    cfg.rx_buffer_size = sizeof(s_rx_buf);
    cfg.line_buffer = small_line;
    cfg.line_buffer_size = sizeof(small_line);
    cfg.default_timeout = 5;
    reset_mock();
    reset_cb();
    at_init(&cfg);

    make_req(&req, "AT", NULL, NULL, 100, 0);
    TF_CHECK_INT(at_query(&req), 0);
    at_input((const uint8_t *)"0123456789012345678901234\r\n", 27);
    TF_CHECK_INT(s_cb_calls, 1);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OVERFLOW);
    tf_end();

    tf_begin("at/响应缓冲截断到 255 字节");
    init_at(5, 0);
    memset(big, 'A', 300);
    big[300] = '\0';
    make_req(&req, "AT", NULL, NULL, 100, 0);
    TF_CHECK_INT(at_query(&req), 0);
    at_input((const uint8_t *)big, 300);
    at_input((const uint8_t *)"\r\nOK\r\n", 6);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OK);
    TF_CHECK_INT(s_cb_len, 255);
    TF_CHECK_INT((int)strlen(at_get_response()), 255);
    tf_end();
}

static void test_uart_pump(void)
{
    at_request_t req;

    tf_begin("at/启动后 at_process 从 UART 泵取");
    init_at(5, 0);
    TF_CHECK_INT(at_start(), 0);
    set_rx("OK\r\n");
    make_req(&req, "AT+P", NULL, NULL, 100, 0);
    TF_CHECK_INT(at_query(&req), 0);
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OK);
    refresh_tx();
    TF_CHECK_STR((const char *)s_tx, "AT+P\r\n");
    tf_end();

    tf_begin("at/分多次泵取拼成完整行");
    init_at(5, 0);
    TF_CHECK_INT(at_start(), 0);
    set_rx("O");
    make_req(&req, "AT+P", NULL, NULL, 100, 0);
    TF_CHECK_INT(at_query(&req), 0);
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK(at_is_busy());
    set_rx("K\r\n");
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK_INT(s_cb_result, AT_RESULT_OK);
    tf_end();

    tf_begin("at/停止后不泵取 UART");
    init_at(5, 0);
    TF_CHECK_INT(at_start(), 0);
    TF_CHECK_INT(at_stop(), 0);
    set_rx("OK\r\n");
    make_req(&req, "AT+P", NULL, NULL, 100, 0);
    TF_CHECK_INT(at_query(&req), 0);
    TF_CHECK_INT(at_process(), 0);
    TF_CHECK(at_is_busy());
    TF_CHECK_INT(s_cb_calls, 0);
    tf_end();
}

void ut_at(void)
{
    test_lifecycle();
    test_send();
    test_query_complete();
    test_cancel();
    test_prompt();
    test_urc_priority();
    test_timeout_retry();
    test_buffer_limits();
    test_uart_pump();
    at_deinit();
}
