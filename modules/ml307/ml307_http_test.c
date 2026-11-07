/* components/modem/ml307/ml307_http.c per-file 单元测试。
 *
 * 覆盖：URL 参数校验、GET/POST 的 AT 命令构造、AT 提示符两阶段发送、
 *       读取响应、响应缓冲上限（受 at 引擎 255 字节响应上限约束）、
 *       各阶段失败时错误向上传播。
 * 未覆盖：真实模组 HTTP 会话与网络超时；at_send_raw 传 NULL body 的实际报文。
 * 说明：通过模拟 UART 按“命令 -> 响应”方式回放；"\r\n" 续发不消耗响应。
 */
#include "test.h"
#include "ml307_http.h"
#include "at.h"
#include "port.h"
#include "port_test.h"

#include <string.h>

#define TX_MAX 4096
#define RESP_QMAX 8

static port_uart_handle_t s_uart;

static uint8_t s_tx[TX_MAX + 1];
static uint32_t s_tx_len;

static const char *s_q[RESP_QMAX];
static int s_qn;
static int s_qi;
static int s_auto_ok;

static char s_line_buf[1024];
static uint8_t s_rx_buf[64];

static void set_rx(const char *resp)
{
    if (resp == NULL) {
        return;
    }
    (void)port_test_uart_inject(&s_uart, (const uint8_t *)resp,
                               (uint32_t)strlen(resp));
}

/* 每次发送后回放一条响应；命令尾部 "\r\n" 且仍有未读数据时不消耗响应。 */
static void uart_tx_hook(void *handle, port_uart_event_t event,
                         size_t length, void *user)
{
    uint32_t total;
    bool is_crlf = false;

    (void)handle;
    (void)user;
    total = port_test_uart_get_tx(&s_uart, s_tx, TX_MAX);
    s_tx_len = total;
    s_tx[total] = '\0';

    if (event != PORT_UART_EVENT_TX_COMPLETE) {
        return;
    }
    if (length == 2 && total >= 2U &&
        s_tx[total - 2U] == '\r' && s_tx[total - 1U] == '\n') {
        is_crlf = true;
    }
    if (is_crlf && port_uart_get_rx_count(&s_uart) > 0U) {
        return;
    }
    if (s_qi < s_qn) {
        set_rx(s_q[s_qi++]);
    } else if (s_auto_ok) {
        set_rx("OK\r\n");
    }
}

static void uart_fixture_reset(void)
{
    port_uart_config_t cfg;

    port_test_reset_state();
    memset(&s_uart, 0, sizeof(s_uart));
    memset(&cfg, 0, sizeof(cfg));
    cfg.baudrate = 115200;
    cfg.word_length = PORT_UART_WORD_8BIT;
    cfg.parity = PORT_UART_PARITY_NONE;
    cfg.stop_bits = PORT_UART_STOP_1;
    cfg.flow_control = PORT_UART_FLOW_NONE;
    cfg.mode = PORT_UART_MODE_TX_RX;
    (void)port_uart_init(&s_uart, &cfg);
    (void)port_uart_register_callback(&s_uart, uart_tx_hook, NULL);
    s_tx_len = 0;
    s_tx[0] = '\0';
}

static void mock_reset(void)
{
    s_qn = 0;
    s_qi = 0;
    s_auto_ok = 1;
    uart_fixture_reset();
}

static void queue_resp(const char *resp)
{
    if (s_qi >= s_qn) {
        s_qi = 0;
        s_qn = 0;
    }
    if (s_qn < RESP_QMAX) {
        s_q[s_qn++] = resp;
    }
}

static int boot_at(void)
{
    at_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx_buf;
    cfg.rx_buffer_size = sizeof(s_rx_buf);
    cfg.line_buffer = s_line_buf;
    cfg.line_buffer_size = sizeof(s_line_buf);
    cfg.default_timeout = 5;

    mock_reset();
    at_deinit();
    if (at_init(&cfg) != 0) {
        return -1;
    }
    return at_start();
}

static void test_validation(void)
{
    at_deinit();

    tf_begin("ml307_http/GET 参数校验");
    TF_CHECK_INT(ml307_http_get(NULL, NULL, NULL), -1);
    TF_CHECK_INT(ml307_http_get("", NULL, NULL), -1);
    tf_end();

    tf_begin("ml307_http/POST 参数校验");
    TF_CHECK_INT(ml307_http_post(NULL, "{}", NULL, NULL), -1);
    TF_CHECK_INT(ml307_http_post("", "{}", NULL, NULL), -1);
    tf_end();

    tf_begin("ml307_http/未初始化时 AT 调用失败");
    TF_CHECK_INT(ml307_http_get("http://h/x", NULL, NULL), -1);
    tf_end();
}

static void test_get(void)
{
    const char *resp = NULL;
    uint32_t len = 0;
    static char big[601];
    static char url[401];

    tf_begin("ml307_http/GET 成功");
    TF_CHECK_INT(boot_at(), 0);
    queue_resp("OK\r\n");                       /* AT+HTTPTERM */
    queue_resp("OK\r\n");                       /* AT+HTTPGET */
    queue_resp("DATA-123\r\nOK\r\n");           /* AT+HTTPREAD */
    TF_CHECK_INT(ml307_http_get("http://x/a", &resp, &len), 0);
    TF_CHECK_STR(resp, "DATA-123");
    TF_CHECK_INT(len, 8);
    TF_CHECK(strstr((const char *)s_tx, "AT+HTTPTERM") != NULL);
    TF_CHECK(strstr((const char *)s_tx, "AT+HTTPGET=http://x/a") != NULL);
    TF_CHECK(strstr((const char *)s_tx, "AT+HTTPREAD=0,1024") != NULL);
    tf_end();

    tf_begin("ml307_http/GET 允许输出参数为 NULL");
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    queue_resp("BODY\r\nOK\r\n");
    TF_CHECK_INT(ml307_http_get("http://x/b", NULL, NULL), 0);
    tf_end();

    tf_begin("ml307_http/GET 命令失败");
    queue_resp("OK\r\n");
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_http_get("http://x/c", &resp, &len), -1);
    tf_end();

    tf_begin("ml307_http/GET 读取失败");
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_http_get("http://x/d", &resp, &len), -1);
    tf_end();

    tf_begin("ml307_http/GET 响应受 at 响应缓冲限制");
    memset(big, 'A', 600);
    big[600] = '\0';
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    queue_resp("A");                             /* 占位，下面用长行替换 */
    {
        static char long_resp[620];
        memcpy(long_resp, big, 600);
        long_resp[600] = '\r';
        long_resp[601] = '\n';
        memcpy(&long_resp[602], "OK\r\n", 5);
        s_q[s_qn - 1] = long_resp;
    }
    TF_CHECK_INT(ml307_http_get("http://x/e", &resp, &len), 0);
    TF_CHECK_INT(len, 255);
    TF_CHECK_INT((int)strlen(resp), 255);
    tf_end();

    tf_begin("ml307_http/超长 URL 被命令缓冲截断");
    memset(url, 'a', 400);
    memcpy(url, "http://h/", 9);
    url[400] = '\0';
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    queue_resp("SMALL\r\nOK\r\n");
    TF_CHECK_INT(ml307_http_get(url, &resp, &len), 0);
    TF_CHECK_STR(resp, "SMALL");
    TF_CHECK(strstr((const char *)s_tx, "AT+HTTPGET=http://h/") != NULL);
    tf_end();

    at_deinit();
}

static void test_post(void)
{
    const char *resp = NULL;
    uint32_t len = 0;

    tf_begin("ml307_http/POST 两阶段发送成功");
    TF_CHECK_INT(boot_at(), 0);
    queue_resp("OK\r\n");                       /* HTTPTERM */
    queue_resp("OK\r\n");                       /* HTTPHEADER */
    queue_resp(">");                            /* HTTPPOST 等待提示符 */
    queue_resp("OK\r\n");                       /* 发送 body 后收到 OK */
    queue_resp("PAYLOAD\r\nOK\r\n");            /* HTTPREAD */
    TF_CHECK_INT(ml307_http_post("http://x/p", "{}", &resp, &len), 0);
    TF_CHECK_STR(resp, "PAYLOAD");
    TF_CHECK_INT(len, 7);
    TF_CHECK(strstr((const char *)s_tx, "AT+HTTPPOST=http://x/p") != NULL);
    TF_CHECK(strstr((const char *)s_tx,
                    "AT+HTTPHEADER=\"Content-Type: application/json\"") != NULL);
    TF_CHECK(strstr((const char *)s_tx, "{}") != NULL);
    tf_end();

    tf_begin("ml307_http/POST body 为 NULL");
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    queue_resp(">");
    queue_resp("OK\r\n");                       /* 单独发送 \r\n 后的应答 */
    TF_CHECK_INT(ml307_http_post("http://x/q", NULL, &resp, &len), 0);
    TF_CHECK_INT(len, 0);
    tf_end();

    tf_begin("ml307_http/POST 阶段1 失败");
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_http_post("http://x/r", "{}", &resp, &len), -1);
    tf_end();

    tf_begin("ml307_http/POST 阶段3 失败");
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    queue_resp(">");
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_http_post("http://x/s", "{}", &resp, &len), -1);
    tf_end();

    at_deinit();
}

void ut_ml307_http(void)
{
    test_validation();
    test_get();
    test_post();
}
