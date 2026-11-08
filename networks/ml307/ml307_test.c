/* components/modem/ml307/ml307.c per-file 单元测试。
 *
 * 覆盖：初始化参数校验与重复初始化、start 成功/失败状态迁移、
 *       stop/reset/process、IMEI/ICCID/IMSI 数字提取与缓冲区截断、
 *       CSQ 信号换算、IP 引号提取、CEREG 注册判断、网络连接/断开状态。
 * 未覆盖：真实模组 AT 交互时序；ml307_reset 后模组真正复位。
 * 说明：通过模拟 UART 按命令回放预置响应；每个数据命令消耗一条响应。
 */
#include "test.h"
#include "ml307.h"
#include "hal.h"
#include "pc.h"

#include <string.h>

#define TX_MAX 2048
#define RESP_QMAX 8

static hal_uart_t s_uart;

static uint8_t s_tx[TX_MAX + 1];
static uint32_t s_tx_len;

static const char *s_q[RESP_QMAX];
static int s_qn;
static int s_qi;
static int s_auto_ok;

static char s_line_buf[512];
static uint8_t s_rx_buf[64];

static void set_rx(const char *resp)
{
    if (resp == NULL) {
        return;
    }
    (void)iot_test_uart_inject(&s_uart, (const uint8_t *)resp,
                               (uint32_t)strlen(resp));
}

/* 每次发送后按旧 mock 语义回放一条响应；命令尾部的 CRLF 不消耗响应。 */
static void uart_tx_hook(void *handle, hal_uart_event_t event,
                         size_t length, void *user)
{
    uint32_t total;
    bool is_crlf = false;

    (void)handle;
    (void)user;
    total = iot_test_uart_get_tx(&s_uart, s_tx, TX_MAX);
    s_tx_len = total;
    s_tx[total] = '\0';

    if (event != HAL_UART_EVENT_TX_COMPLETE) {
        return;
    }
    if (length == 2 && total >= 2U &&
        s_tx[total - 2U] == '\r' && s_tx[total - 1U] == '\n') {
        is_crlf = true;
    }
    if (is_crlf) {
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
    (void)hal_uart_register_callback(&s_uart, uart_tx_hook, NULL);
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

static at_config_t make_cfg(void)
{
    at_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = &s_uart;
    cfg.rx_buffer = s_rx_buf;
    cfg.rx_buffer_size = sizeof(s_rx_buf);
    cfg.line_buffer = s_line_buf;
    cfg.line_buffer_size = sizeof(s_line_buf);
    cfg.default_timeout = 5;
    return cfg;
}

static int boot(void)
{
    at_config_t cfg = make_cfg();

    mock_reset();
    at_deinit();
    return ml307_init(&cfg);
}

static void test_lifecycle(void)
{
    at_config_t cfg = make_cfg();
    at_config_t bad;

    tf_begin("ml307/初始化参数校验");
    at_deinit();
    TF_CHECK_INT(ml307_init(NULL), -1);
    memset(&bad, 0, sizeof(bad));
    TF_CHECK_INT(ml307_init(&bad), -1);      /* uart == NULL */
    mock_reset();
    TF_CHECK_INT(ml307_init(&cfg), 0);
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_OFF);
    TF_CHECK(!ml307_network_is_connected());
    tf_end();

    tf_begin("ml307/重复初始化与 process");
    TF_CHECK_INT(ml307_init(&cfg), 0);
    TF_CHECK_INT(ml307_process(), 0);
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_OFF);
    tf_end();

    tf_begin("ml307/stop 幂等并复位状态");
    TF_CHECK_INT(ml307_stop(), 0);
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_OFF);
    TF_CHECK_INT(ml307_stop(), 0);
    TF_CHECK(!ml307_network_is_connected());
    tf_end();

    tf_begin("ml307/reset 回到 OFF");
    mock_reset();
    TF_CHECK_INT(ml307_reset(), 0);
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_OFF);
    TF_CHECK(!ml307_network_is_connected());
    tf_end();

    at_deinit();
}

static void test_start(void)
{
    tf_begin("ml307/start 成功进入 READY");
    TF_CHECK_INT(boot(), 0);
    TF_CHECK_INT(ml307_start(), 0);
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_READY);
    TF_CHECK(strstr((const char *)s_tx, "AT\r\n") != NULL);
    TF_CHECK(strstr((const char *)s_tx, "ATE0\r\n") != NULL);
    TF_CHECK_INT(ml307_stop(), 0);
    tf_end();

    tf_begin("ml307/start 无响应进入 ERROR");
    TF_CHECK_INT(boot(), 0);
    s_auto_ok = 0;
    TF_CHECK_INT(ml307_start(), -1);
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_ERROR);
    tf_end();

    tf_begin("ml307/未初始化时 start 失败");
    at_deinit();
    TF_CHECK_INT(ml307_start(), -1);
    tf_end();

    at_deinit();
}

static void test_identity(void)
{
    char buf[32];
    ml307_info_t info;

    tf_begin("ml307/读取 IMEI");
    TF_CHECK_INT(boot(), 0);
    TF_CHECK_INT(ml307_start(), 0);
    queue_resp("+CGSN: 861234567890123\r\nOK\r\n");
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(ml307_get_imei(buf, sizeof(buf)), 0);
    TF_CHECK_STR(buf, "861234567890123");
    tf_end();

    tf_begin("ml307/IMEI 缓冲区截断");
    queue_resp("+CGSN: 861234567890123\r\nOK\r\n");
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(ml307_get_imei(buf, 5), 0);
    TF_CHECK_STR(buf, "8612");
    tf_end();

    tf_begin("ml307/IMEI 参数非法");
    queue_resp("+CGSN: 861234567890123\r\nOK\r\n");
    TF_CHECK_INT(ml307_get_imei(NULL, 32), -1);
    tf_end();

    tf_begin("ml307/读取 ICCID 与 IMSI");
    queue_resp("+ICCID: 8986001234567890123\r\nOK\r\n");
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(ml307_get_iccid(buf, sizeof(buf)), 0);
    TF_CHECK_STR(buf, "8986001234567890123");

    queue_resp("+CIMI: 460001234567890\r\nOK\r\n");
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(ml307_get_imsi(buf, sizeof(buf)), 0);
    TF_CHECK_STR(buf, "460001234567890");
    tf_end();

    tf_begin("ml307/CSQ 信号换算");
    memset(&info, 0, sizeof(info));
    queue_resp("+CSQ: 20,99\r\nOK\r\n");
    TF_CHECK_INT(ml307_get_signal(&info), 0);
    TF_CHECK_INT(info.rssi, -73);
    TF_CHECK_INT(ml307_get_signal(NULL), -1);
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_get_signal(&info), -1);
    queue_resp("+CSQ: abc\r\nOK\r\n");
    TF_CHECK_INT(ml307_get_signal(&info), -1);
    tf_end();

    tf_begin("ml307/读取 IP");
    queue_resp("+CGPADDR: 1,\"10.0.0.5\"\r\nOK\r\n");
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(ml307_get_ip(buf, sizeof(buf)), 0);
    TF_CHECK_STR(buf, "10.0.0.5");

    queue_resp("+CGPADDR: 1,\"10.0.0.5\"\r\nOK\r\n");
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(ml307_get_ip(buf, 5), 0);
    TF_CHECK_STR(buf, "10.0");

    queue_resp("OK\r\n");
    TF_CHECK_INT(ml307_get_ip(buf, sizeof(buf)), -1);
    TF_CHECK_INT(ml307_get_ip(NULL, 10), -1);
    TF_CHECK_INT(ml307_get_ip(buf, 0), -1);
    tf_end();

    tf_begin("ml307/命令失败向上传播");
    s_auto_ok = 0;
    TF_CHECK_INT(ml307_get_imei(buf, sizeof(buf)), -1);
    tf_end();

    at_deinit();
}

static void test_network(void)
{
    pdp_config_t pdp;

    tf_begin("ml307/CEREG 注册判断");
    TF_CHECK_INT(boot(), 0);
    TF_CHECK_INT(ml307_start(), 0);

    queue_resp("+CEREG: 0,1\r\nOK\r\n");
    TF_CHECK(ml307_network_is_registered());
    queue_resp("+CEREG: 0,5\r\nOK\r\n");
    TF_CHECK(ml307_network_is_registered());
    queue_resp("+CEREG: 0,2\r\nOK\r\n");
    TF_CHECK(!ml307_network_is_registered());
    queue_resp("ERROR\r\n");
    TF_CHECK(!ml307_network_is_registered());
    tf_end();

    tf_begin("ml307/无 APN 连接并进入 REGISTERING");
    queue_resp("OK\r\n");
    queue_resp("+CEREG: 0,2\r\nOK\r\n");
    TF_CHECK_INT(ml307_network_connect(NULL), 0);
    TF_CHECK(ml307_network_is_connected());
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_REGISTERING);
    tf_end();

    tf_begin("ml307/带 APN 连接并已注册");
    memset(&pdp, 0, sizeof(pdp));
    strcpy(pdp.apn, "ctnet");
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    queue_resp("+CEREG: 0,1\r\nOK\r\n");
    TF_CHECK_INT(ml307_network_connect(&pdp), 0);
    TF_CHECK(ml307_network_is_connected());
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_CONNECTED);
    TF_CHECK(strstr((const char *)s_tx, "AT+CGDCONT=1,\"IP\",\"ctnet\"") != NULL);
    tf_end();

    tf_begin("ml307/连接失败不改变连接标志");
    TF_CHECK_INT(boot(), 0);
    TF_CHECK_INT(ml307_start(), 0);
    s_auto_ok = 0;
    TF_CHECK_INT(ml307_network_connect(NULL), -1);
    TF_CHECK(!ml307_network_is_connected());
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_READY);
    tf_end();

    tf_begin("ml307/断开网络");
    s_auto_ok = 1;
    TF_CHECK_INT(ml307_network_disconnect(), 0);
    TF_CHECK(!ml307_network_is_connected());
    TF_CHECK_INT(ml307_get_state(), ML307_STATE_READY);
    tf_end();

    tf_begin("ml307/未初始化时网络操作失败");
    at_deinit();
    TF_CHECK(!ml307_network_is_registered());
    TF_CHECK_INT(ml307_network_connect(NULL), -1);
    tf_end();
}

static void test_extended(void)
{
    ml307_wifi_ap_t aps[4];
    uint8_t count = 0xFFU;
    int16_t csq = 0;
    int16_t ber = 0;

    tf_begin("ml307/CSQ 原始信号");
    TF_CHECK_INT(boot(), 0);
    TF_CHECK_INT(ml307_start(), 0);
    queue_resp("+CSQ: 22,99\r\nOK\r\n");
    TF_CHECK_INT(ml307_get_csq(&csq, &ber), 0);
    TF_CHECK_INT(csq, 22);
    TF_CHECK_INT(ber, 99);
    queue_resp("+CSQ: 99,99\r\nOK\r\n");
    TF_CHECK_INT(ml307_get_csq(&csq, NULL), 0);
    TF_CHECK_INT(csq, 99);
    queue_resp("+CSQ: bad\r\nOK\r\n");
    TF_CHECK_INT(ml307_get_csq(&csq, &ber), -1);
    TF_CHECK_INT(ml307_get_csq(NULL, &ber), -1);
    tf_end();

    tf_begin("ml307/设置 APN");
    queue_resp("OK\r\n");
    TF_CHECK_INT(ml307_set_apn("cmnet", NULL, NULL), 0);
    TF_CHECK(strstr((const char *)s_tx,
                    "AT+CGDCONT=1,\"IP\",\"cmnet\"") != NULL);
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    TF_CHECK_INT(ml307_set_apn("cmnet", "user", "pass"), 0);
    TF_CHECK(strstr((const char *)s_tx,
                    "AT+CGAUTH=1,1,\"user\",\"pass\"") != NULL);
    TF_CHECK_INT(ml307_set_apn(NULL, NULL, NULL), -1);
    TF_CHECK_INT(ml307_set_apn("", NULL, NULL), -1);
    TF_CHECK_INT(ml307_set_apn("bad\"apn", NULL, NULL), -1);
    tf_end();

    tf_begin("ml307/WiFi 列表解析");
    queue_resp("+WIFISCAN: \"CMCC-1\",aa:bb:cc:dd:ee:01,-45,6\r\n"
               "+WIFISCAN: \"CMCC-2\",aa:bb:cc:dd:ee:02,-60,11\r\n"
               "OK\r\n");
    TF_CHECK_INT(ml307_get_wifi_list(aps, 4, &count), 0);
    TF_CHECK_INT(count, 2);
    TF_CHECK_STR(aps[0].ssid, "CMCC-1");
    TF_CHECK_STR(aps[0].bssid, "aa:bb:cc:dd:ee:01");
    TF_CHECK_INT(aps[0].rssi, -45);
    TF_CHECK_INT(aps[0].channel, 6);
    TF_CHECK_INT(aps[1].rssi, -60);
    TF_CHECK_INT(aps[1].channel, 11);

    queue_resp("+WIFISCAN: \"CMCC-1\",aa:bb:cc:dd:ee:01,-45,6\r\n"
               "+WIFISCAN: \"CMCC-2\",aa:bb:cc:dd:ee:02,-60,11\r\n"
               "OK\r\n");
    TF_CHECK_INT(ml307_get_wifi_list(aps, 1, &count), 0);
    TF_CHECK_INT(count, 1);

    queue_resp("+WIFISCAN: ,aa:bb:cc:dd:ee:03,-50,1\r\nOK\r\n");
    TF_CHECK_INT(ml307_get_wifi_list(aps, 4, &count), 0);
    TF_CHECK_INT(count, 0);

    TF_CHECK_INT(ml307_get_wifi_list(NULL, 0, &count), 0);
    TF_CHECK_INT(count, 0);
    TF_CHECK_INT(ml307_get_wifi_list(aps, 4, NULL), -1);
    TF_CHECK_INT(ml307_get_wifi_list(NULL, 4, &count), -1);
    tf_end();

    at_deinit();
}

void ut_ml307(void)
{
    test_lifecycle();
    test_start();
    test_identity();
    test_network();
    test_extended();
    at_deinit();
}
