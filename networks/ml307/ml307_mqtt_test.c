/* components/modem/ml307/ml307_mqtt.c per-file 单元测试。
 *
 * 覆盖：连接/订阅/退订/发布的参数校验、AT 命令构造（含缺省字符串与 QoS/retain）、
 *       发布的两阶段 "> " 发送流程、未初始化时的错误传播、各阶段失败。
 * 未覆盖：真实模组 MQTT 会话与网络超时。
 */
#include "test.h"
#include "ml307_mqtt.h"
#include "at.h"
#include "hal.h"
#include "pc.h"

#include <string.h>

#define TX_MAX 4096
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

/* 每次发送后回放一条响应；命令尾部 "\r\n" 且仍有未读数据时不消耗响应。 */
static void uart_tx_hook(void *handle, hal_uart_event_e event,
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
    if (is_crlf && hal_uart_get_rx_count(&s_uart) > 0U) {
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

    tf_begin("ml307_mqtt/连接参数校验");
    TF_CHECK_INT(ml307_mqtt_connect(NULL, 1883, "c", NULL, NULL), -1);
    TF_CHECK_INT(ml307_mqtt_connect("", 1883, "c", NULL, NULL), -1);
    tf_end();

    tf_begin("ml307_mqtt/订阅参数校验");
    TF_CHECK_INT(ml307_mqtt_subscribe(NULL, 0), -1);
    TF_CHECK_INT(ml307_mqtt_subscribe("", 0), -1);
    TF_CHECK_INT(ml307_mqtt_unsubscribe(NULL), -1);
    TF_CHECK_INT(ml307_mqtt_unsubscribe(""), -1);
    tf_end();

    tf_begin("ml307_mqtt/发布参数校验");
    TF_CHECK_INT(ml307_mqtt_publish(NULL, (const uint8_t *)"x", 1, 0, false), -1);
    TF_CHECK_INT(ml307_mqtt_publish("t", NULL, 1, 0, false), -1);
    TF_CHECK_INT(ml307_mqtt_publish("t", (const uint8_t *)"x", 0, 0, false), -1);
    tf_end();

    tf_begin("ml307_mqtt/未初始化时调用失败");
    TF_CHECK_INT(ml307_mqtt_connect("h", 1883, "c", NULL, NULL), -1);
    TF_CHECK_INT(ml307_mqtt_disconnect(), -1);
    TF_CHECK_INT(ml307_mqtt_subscribe("t", 0), -1);
    TF_CHECK_INT(ml307_mqtt_unsubscribe("t"), -1);
    TF_CHECK_INT(ml307_mqtt_publish("t", (const uint8_t *)"x", 1, 0, false), -1);
    tf_end();
}

static void test_connect(void)
{
    tf_begin("ml307_mqtt/连接命令构造");
    TF_CHECK_INT(boot_at(), 0);
    queue_resp("OK\r\n");            /* AT+MQTTDISC */
    queue_resp("OK\r\n");            /* AT+MQTTCONN */
    TF_CHECK_INT(ml307_mqtt_connect("broker.local", 1883, "cid", "user", "pass"),
                 0);
    TF_CHECK(strstr((const char *)s_tx, "AT+MQTTDISC=0") != NULL);
    TF_CHECK(strstr((const char *)s_tx,
        "AT+MQTTCONN=0,\"broker.local\",1883,\"cid\",\"user\",\"pass\"") != NULL);
    tf_end();

    tf_begin("ml307_mqtt/连接时空字符串参数");
    queue_resp("OK\r\n");
    queue_resp("OK\r\n");
    TF_CHECK_INT(ml307_mqtt_connect("h", 1, NULL, NULL, NULL), 0);
    TF_CHECK(strstr((const char *)s_tx,
        "AT+MQTTCONN=0,\"h\",1,\"\",\"\",\"\"") != NULL);
    tf_end();

    tf_begin("ml307_mqtt/连接失败");
    queue_resp("OK\r\n");
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_mqtt_connect("h", 1883, "c", NULL, NULL), -1);
    tf_end();

    tf_begin("ml307_mqtt/断开连接");
    queue_resp("OK\r\n");
    TF_CHECK_INT(ml307_mqtt_disconnect(), 0);
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_mqtt_disconnect(), -1);
    tf_end();

    at_deinit();
}

static void test_sub_pub(void)
{
    tf_begin("ml307_mqtt/订阅与退订命令构造");
    TF_CHECK_INT(boot_at(), 0);
    queue_resp("OK\r\n");
    TF_CHECK_INT(ml307_mqtt_subscribe("t/1", 1), 0);
    TF_CHECK(strstr((const char *)s_tx, "AT+MQTTSUB=0,\"t/1\",1") != NULL);

    queue_resp("OK\r\n");
    TF_CHECK_INT(ml307_mqtt_unsubscribe("t/1"), 0);
    TF_CHECK(strstr((const char *)s_tx, "AT+MQTTUNSUB=0,\"t/1\"") != NULL);

    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_mqtt_subscribe("t/2", 0), -1);
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_mqtt_unsubscribe("t/2"), -1);
    tf_end();

    tf_begin("ml307_mqtt/发布 QoS0 命令与载荷");
    queue_resp(">");                 /* 命令等待提示符 */
    queue_resp("OK\r\n");            /* 发送载荷后 */
    TF_CHECK_INT(ml307_mqtt_publish("t", (const uint8_t *)"hi", 2, 0, false),
                 0);
    TF_CHECK(strstr((const char *)s_tx, "AT+MQTTPUB=0,\"t\",0,0,0,2") != NULL);
    TF_CHECK(strstr((const char *)s_tx, "hi") != NULL);
    tf_end();

    tf_begin("ml307_mqtt/发布 QoS1 + retain 命令");
    queue_resp(">");
    queue_resp("OK\r\n");
    TF_CHECK_INT(ml307_mqtt_publish("t", (const uint8_t *)"x", 1, 1, true),
                 0);
    TF_CHECK(strstr((const char *)s_tx, "AT+MQTTPUB=0,\"t\",1,1,0,1") != NULL);
    tf_end();

    tf_begin("ml307_mqtt/发布阶段1 失败");
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_mqtt_publish("t", (const uint8_t *)"x", 1, 0, false),
                 -1);
    tf_end();

    tf_begin("ml307_mqtt/发布阶段3 失败");
    queue_resp(">");
    queue_resp("ERROR\r\n");
    TF_CHECK_INT(ml307_mqtt_publish("t", (const uint8_t *)"x", 1, 0, false),
                 -1);
    tf_end();

    at_deinit();
}

void ut_ml307_mqtt(void)
{
    test_validation();
    test_connect();
    test_sub_pub();
}
