/* MQTT 3.1.1 客户端测试：使用脚本化模拟传输。 */
#include "test.h"
#include "mqtt.h"

#include <string.h>

typedef struct {
    uint8_t in[512];
    uint32_t in_len;
    uint32_t in_pos;

    uint8_t out[2048];
    uint32_t out_len;
} mqtt_mock_t;

static int mock_write(void *ctx, const uint8_t *data, uint32_t len)
{
    mqtt_mock_t *m = (mqtt_mock_t *)ctx;
    if (m->out_len + len > sizeof(m->out)) {
        return -1;
    }
    memcpy(&m->out[m->out_len], data, len);
    m->out_len += len;
    return (int)len;
}

static int mock_read(void *ctx, uint8_t *data, uint32_t len, uint32_t timeout)
{
    mqtt_mock_t *m = (mqtt_mock_t *)ctx;
    uint32_t avail;
    uint32_t n;
    (void)timeout;

    avail = m->in_len - m->in_pos;
    if (avail == 0) {
        return 0;
    }
    n = (len < avail) ? len : avail;
    memcpy(data, &m->in[m->in_pos], n);
    m->in_pos += n;
    return (int)n;
}

static void mock_set_input(mqtt_mock_t *m, const uint8_t *data, uint32_t len)
{
    memcpy(m->in, data, len);
    m->in_len = len;
    m->in_pos = 0;
}

static void mock_clear_out(mqtt_mock_t *m)
{
    m->out_len = 0;
}

static void make_client(mqtt_client_t *client, mqtt_mock_t *mock,
                        mqtt_message_fn on_message, void *user)
{
    mqtt_config_t cfg;
    mqtt_transport_t transport;

    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.client_id, "dev1");
    strcpy(cfg.host, "broker");
    cfg.port = 1883;
    cfg.keepalive = 60;
    cfg.clean_session = true;

    transport.write = mock_write;
    transport.read = mock_read;
    transport.tick = NULL;
    transport.ctx = mock;

    mqtt_client_init(client, &transport, &cfg, on_message, user);
}

static void test_connect(void)
{
    static mqtt_client_t client;
    static mqtt_mock_t mock;
    static const uint8_t connack_ok[4] = { 0x20, 0x02, 0x00, 0x00 };
    static const uint8_t connack_bad[4] = { 0x20, 0x02, 0x00, 0x05 };

    tf_begin("mqtt/CONNECT 报文与 CONNACK");
    memset(&mock, 0, sizeof(mock));
    make_client(&client, &mock, NULL, NULL);
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    TF_CHECK_INT(mqtt_client_connect(&client), 0);
    TF_CHECK(mqtt_client_is_connected(&client));

    /* CONNECT 固定头 + 协议名 */
    TF_CHECK_INT(mock.out[0], 0x10);
    TF_CHECK_INT(mock.out[2], 0x00);
    TF_CHECK_INT(mock.out[3], 0x04);
    TF_CHECK_MEM(&mock.out[4], "MQTT", 4);
    TF_CHECK_INT(mock.out[8], 0x04);   /* 协议级别 3.1.1 */
    tf_end();

    tf_begin("mqtt/CONNACK 失败返回认证错误");
    memset(&mock, 0, sizeof(mock));
    make_client(&client, &mock, NULL, NULL);
    mock_set_input(&mock, connack_bad, sizeof(connack_bad));
    TF_CHECK_INT(mqtt_client_connect(&client), -1);
    TF_CHECK(!mqtt_client_is_connected(&client));
    tf_end();

    tf_begin("mqtt/未连接操作被拒绝");
    memset(&mock, 0, sizeof(mock));
    make_client(&client, &mock, NULL, NULL);
    TF_CHECK_INT(mqtt_client_subscribe(&client, "t", 0), -1);
    {
        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));
        strcpy(msg.topic, "t");
        TF_CHECK_INT(mqtt_client_publish(&client, &msg), -1);
    }
    tf_end();
}

static void test_pubsub(void)
{
    static mqtt_client_t client;
    static mqtt_mock_t mock;
    static const uint8_t connack_ok[4] = { 0x20, 0x02, 0x00, 0x00 };
    static const uint8_t suback[5] = { 0x90, 0x03, 0x00, 0x01, 0x00 };
    static const uint8_t puback[4] = { 0x40, 0x02, 0x00, 0x02 };
    mqtt_message_t msg;

    memset(&mock, 0, sizeof(mock));
    make_client(&client, &mock, NULL, NULL);
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    mqtt_client_connect(&client);

    tf_begin("mqtt/SUBSCRIBE 与 SUBACK");
    mock_clear_out(&mock);
    mock_set_input(&mock, suback, sizeof(suback));
    TF_CHECK_INT(mqtt_client_subscribe(&client, "sensor/temp", 0), 0);
    TF_CHECK_INT(mock.out[0], 0x82);
    tf_end();

    tf_begin("mqtt/PUBLISH QoS0");
    mock_clear_out(&mock);
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.topic, "t");
    msg.payload = (const uint8_t *)"hi";
    msg.payload_len = 2;
    msg.qos = 0;
    TF_CHECK_INT(mqtt_client_publish(&client, &msg), 0);
    TF_CHECK_INT(mock.out[0], 0x30);
    tf_end();

    tf_begin("mqtt/PUBLISH QoS1 与 PUBACK");
    mock_clear_out(&mock);
    mock_set_input(&mock, puback, sizeof(puback));
    msg.qos = 1;
    TF_CHECK_INT(mqtt_client_publish(&client, &msg), 0);
    TF_CHECK_INT(mock.out[0], 0x32);
    tf_end();

    tf_begin("mqtt/UNSUBSCRIBE 与 UNSUBACK");
    {
        static const uint8_t unsuback[4] = { 0xB0, 0x02, 0x00, 0x03 };
        mock_clear_out(&mock);
        mock_set_input(&mock, unsuback, sizeof(unsuback));
        TF_CHECK_INT(mqtt_client_unsubscribe(&client, "sensor/temp"), 0);
        TF_CHECK_INT(mock.out[0], 0xA2);
    }
    tf_end();
}

static int s_msg_count = 0;
static char s_last_topic[64];
static char s_last_payload[64];
static uint8_t s_last_qos = 0xFF;

static void on_message(const mqtt_message_t *message, void *user)
{
    uint32_t n;
    (void)user;
    s_msg_count++;
    strncpy(s_last_topic, message->topic, sizeof(s_last_topic) - 1);
    s_last_topic[sizeof(s_last_topic) - 1] = '\0';
    n = message->payload_len;
    if (n > sizeof(s_last_payload) - 1) {
        n = sizeof(s_last_payload) - 1;
    }
    memcpy(s_last_payload, message->payload, n);
    s_last_payload[n] = '\0';
    s_last_qos = message->qos;
}

static void test_incoming(void)
{
    static mqtt_client_t client;
    static mqtt_mock_t mock;
    static const uint8_t connack_ok[4] = { 0x20, 0x02, 0x00, 0x00 };
    /* PUBLISH QoS0: topic "a/b"，payload "hello" */
    static const uint8_t publish_qos0[] = {
        0x30, 0x0A, 0x00, 0x03, 'a', '/', 'b', 'h', 'e', 'l', 'l', 'o'
    };
    /* PUBLISH QoS1: topic "a"，pid=7，payload "x" */
    static const uint8_t publish_qos1[] = {
        0x32, 0x06, 0x00, 0x01, 'a', 0x00, 0x07, 'x'
    };

    tf_begin("mqtt/接收 PUBLISH QoS0");
    memset(&mock, 0, sizeof(mock));
    make_client(&client, &mock, on_message, NULL);
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    mqtt_client_connect(&client);

    s_msg_count = 0;
    s_last_topic[0] = '\0';
    s_last_payload[0] = '\0';
    s_last_qos = 0xFF;
    mock_clear_out(&mock);
    mock_set_input(&mock, publish_qos0, sizeof(publish_qos0));
    TF_CHECK_INT(mqtt_client_process(&client), 0);
    TF_CHECK_INT(s_msg_count, 1);
    TF_CHECK_STR(s_last_topic, "a/b");
    TF_CHECK_STR(s_last_payload, "hello");
    TF_CHECK_INT(s_last_qos, 0);
    TF_CHECK_INT(mock.out_len, 0);     /* QoS0 无需确认 */
    tf_end();

    tf_begin("mqtt/接收 PUBLISH QoS1 回 PUBACK");
    s_msg_count = 0;
    mock_clear_out(&mock);
    mock_set_input(&mock, publish_qos1, sizeof(publish_qos1));
    TF_CHECK_INT(mqtt_client_process(&client), 0);
    TF_CHECK_INT(s_msg_count, 1);
    TF_CHECK_STR(s_last_topic, "a");
    TF_CHECK_STR(s_last_payload, "x");
    TF_CHECK_INT(s_last_qos, 1);
    TF_CHECK_INT(mock.out_len, 4);
    TF_CHECK_INT(mock.out[0], 0x40);
    TF_CHECK_INT(mock.out[2], 0x00);
    TF_CHECK_INT(mock.out[3], 0x07);
    tf_end();

    tf_begin("mqtt/断开与重置");
    TF_CHECK_INT(mqtt_client_disconnect(&client), 0);
    TF_CHECK(!mqtt_client_is_connected(&client));
    mqtt_client_reset(&client);
    TF_CHECK(!mqtt_client_is_connected(&client));
    tf_end();
}

void test_mqtt(void)
{
    test_connect();
    test_pubsub();
    test_incoming();
}
