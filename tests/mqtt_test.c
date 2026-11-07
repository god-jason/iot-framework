/* components/modem/ml307/mqtt.c per-file 单元测试。
 *
 * 覆盖：客户端初始化校验、CONNECT/CONNACK 字节级断言与标志位、
 *       订阅/退订报文与应答校验、QoS0/QoS1 发布与 PUBACK、
 *       接收 PUBLISH 分发（QoS0/QoS1/retain/主题截断/畸形帧）、
 *       多报文泵取、keepalive PINGREQ、断开与重置。
 * 未覆盖：TLS、真实网络阻塞时序、QoS2 流程（实现仅支持到 QoS1）。
 */
#include "test_framework.h"
#include "mqtt.h"

#include <string.h>

#define IN_MAX 4096
#define OUT_MAX 4096

typedef struct {
    uint8_t in[IN_MAX];
    uint32_t in_len;
    uint32_t in_pos;

    uint8_t out[OUT_MAX];
    uint32_t out_len;

    uint32_t tick_now;
    int fail_write;
    uint32_t max_chunk;
} mqtt_mock_t;

static int mk_write(void *ctx, const uint8_t *data, uint32_t len)
{
    mqtt_mock_t *m = (mqtt_mock_t *)ctx;

    if (m->fail_write) {
        return -1;
    }
    if (m->out_len + len > sizeof(m->out)) {
        return -1;
    }
    memcpy(&m->out[m->out_len], data, len);
    m->out_len += len;
    return (int)len;
}

static int mk_read(void *ctx, uint8_t *data, uint32_t len, uint32_t timeout)
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
    if (m->max_chunk > 0 && n > m->max_chunk) {
        n = m->max_chunk;
    }
    memcpy(data, &m->in[m->in_pos], n);
    m->in_pos += n;
    return (int)n;
}

static uint32_t mk_tick(void *ctx)
{
    return ((mqtt_mock_t *)ctx)->tick_now;
}

static void mock_reset(mqtt_mock_t *m)
{
    memset(m, 0, sizeof(*m));
}

static void mock_set_input(mqtt_mock_t *m, const uint8_t *data, uint32_t len)
{
    memcpy(m->in, data, len);
    m->in_len = len;
    m->in_pos = 0;
}

static void mock_append_input(mqtt_mock_t *m, const uint8_t *data, uint32_t len)
{
    uint32_t pos = m->in_len;

    memcpy(&m->in[pos], data, len);
    m->in_len = pos + len;
}

static void mock_clear_out(mqtt_mock_t *m)
{
    m->out_len = 0;
}

static void default_config(mqtt_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->host, "broker");
    cfg->port = 1883;
    strcpy(cfg->client_id, "dev1");
    cfg->keepalive = 60;
    cfg->clean_session = true;
}

static void make_client(mqtt_client_t *client, mqtt_mock_t *mock,
                        const mqtt_config_t *cfg,
                        mqtt_message_fn on_message, void *user)
{
    mqtt_transport_t transport;

    transport.write = mk_write;
    transport.read = mk_read;
    transport.tick = NULL;
    transport.ctx = mock;
    mqtt_client_init(client, &transport, cfg, on_message, user);
}

static mqtt_client_t s_client;

static void test_init(void)
{
    static const uint8_t connack_ok[4] = { 0x20, 0x02, 0x00, 0x00 };
    mqtt_mock_t mock;
    mqtt_config_t cfg;
    mqtt_transport_t t;

    default_config(&cfg);
    mock_reset(&mock);

    tf_begin("mqtt/初始化参数校验");
    memset(&t, 0, sizeof(t));
    t.write = mk_write;
    t.read = mk_read;
    t.ctx = &mock;
    TF_CHECK_INT(mqtt_client_init(NULL, &t, &cfg, NULL, NULL), -1);
    TF_CHECK_INT(mqtt_client_init(&s_client, NULL, &cfg, NULL, NULL), -1);
    TF_CHECK_INT(mqtt_client_init(&s_client, &t, NULL, NULL, NULL), -1);
    {
        mqtt_transport_t bad = t;
        bad.write = NULL;
        TF_CHECK_INT(mqtt_client_init(&s_client, &bad, &cfg, NULL, NULL), -1);
        bad = t;
        bad.read = NULL;
        TF_CHECK_INT(mqtt_client_init(&s_client, &bad, &cfg, NULL, NULL), -1);
    }
    TF_CHECK_INT(mqtt_client_init(&s_client, &t, &cfg, NULL, NULL), IOT_OK);
    TF_CHECK(!mqtt_client_is_connected(&s_client));
    TF_CHECK_INT(s_client.next_packet_id, 1);
    tf_end();

    tf_begin("mqtt/未连接时操作被拒绝");
    TF_CHECK_INT(mqtt_client_subscribe(&s_client, "t", 0), -1);
    TF_CHECK_INT(mqtt_client_unsubscribe(&s_client, "t"), -1);
    {
        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));
        strcpy(msg.topic, "t");
        TF_CHECK_INT(mqtt_client_publish(&s_client, &msg), -1);
    }
    TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
    tf_end();

    tf_begin("mqtt/重复初始化重置连接状态");
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    TF_CHECK_INT(mqtt_client_connect(&s_client), IOT_OK);
    TF_CHECK(mqtt_client_is_connected(&s_client));
    mock_reset(&mock);
    TF_CHECK_INT(mqtt_client_init(&s_client, &t, &cfg, NULL, NULL), IOT_OK);
    TF_CHECK(!mqtt_client_is_connected(&s_client));
    tf_end();
}

static void test_connect(void)
{
    static const uint8_t connack_ok[4] = { 0x20, 0x02, 0x00, 0x00 };
    static const uint8_t connack_bad[4] = { 0x20, 0x02, 0x00, 0x05 };
    static const uint8_t connack_short[3] = { 0x20, 0x01, 0x00 };
    static const uint8_t not_connack[4] = { 0x40, 0x02, 0x00, 0x00 };
    mqtt_mock_t mock;
    mqtt_config_t cfg;

    tf_begin("mqtt/CONNECT 报文与 CONNACK");
    mock_reset(&mock);
    default_config(&cfg);
    make_client(&s_client, &mock, &cfg, NULL, NULL);
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    TF_CHECK_INT(mqtt_client_connect(&s_client), IOT_OK);
    TF_CHECK(mqtt_client_is_connected(&s_client));
    TF_CHECK_INT(mock.out_len, 18);
    TF_CHECK_INT(mock.out[0], 0x10);
    TF_CHECK_INT(mock.out[2], 0x00);
    TF_CHECK_INT(mock.out[3], 0x04);
    TF_CHECK_MEM(&mock.out[4], "MQTT", 4);
    TF_CHECK_INT(mock.out[8], 0x04);       /* 3.1.1 */
    TF_CHECK_INT(mock.out[9], 0x02);       /* clean session */
    TF_CHECK_INT(mock.out[10], 0x00);
    TF_CHECK_INT(mock.out[11], 60);        /* keepalive */
    TF_CHECK_INT(mock.out[12], 0x00);
    TF_CHECK_INT(mock.out[13], 0x04);
    TF_CHECK_MEM(&mock.out[14], "dev1", 4);
    tf_end();

    tf_begin("mqtt/CONNACK 返回码非 0");
    mock_reset(&mock);
    make_client(&s_client, &mock, &cfg, NULL, NULL);
    mock_set_input(&mock, connack_bad, sizeof(connack_bad));
    TF_CHECK_INT(mqtt_client_connect(&s_client), -1);
    TF_CHECK(!mqtt_client_is_connected(&s_client));
    tf_end();

    tf_begin("mqtt/CONNACK 过短、类型错误、无数据");
    mock_reset(&mock);
    make_client(&s_client, &mock, &cfg, NULL, NULL);
    mock_set_input(&mock, connack_short, sizeof(connack_short));
    TF_CHECK_INT(mqtt_client_connect(&s_client), -1);

    mock_reset(&mock);
    make_client(&s_client, &mock, &cfg, NULL, NULL);
    mock_set_input(&mock, not_connack, sizeof(not_connack));
    TF_CHECK_INT(mqtt_client_connect(&s_client), -1);

    mock_reset(&mock);
    make_client(&s_client, &mock, &cfg, NULL, NULL);
    TF_CHECK_INT(mqtt_client_connect(&s_client), -1);
    tf_end();

    tf_begin("mqtt/用户名密码标志位");
    mock_reset(&mock);
    default_config(&cfg);
    strcpy(cfg.username, "u");
    strcpy(cfg.password, "p");
    make_client(&s_client, &mock, &cfg, NULL, NULL);
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    TF_CHECK_INT(mqtt_client_connect(&s_client), IOT_OK);
    TF_CHECK_INT(mock.out[9], 0xC2);       /* clean + user + pass */
    TF_CHECK(strstr((const char *)&mock.out[14], "dev1") != NULL);
    tf_end();

    tf_begin("mqtt/写失败与空 client_id");
    mock_reset(&mock);
    default_config(&cfg);
    cfg.client_id[0] = '\0';
    cfg.clean_session = false;
    cfg.keepalive = 0;
    make_client(&s_client, &mock, &cfg, NULL, NULL);
    mock.fail_write = 1;
    TF_CHECK_INT(mqtt_client_connect(&s_client), -1);

    mock.fail_write = 0;
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    TF_CHECK_INT(mqtt_client_connect(&s_client), IOT_OK);
    TF_CHECK_INT(mock.out[9], 0x00);
    TF_CHECK_INT(mock.out[10], 0x00);
    TF_CHECK_INT(mock.out[11], 0x00);
    TF_CHECK_INT(mock.out[12], 0x00);      /* client_id 长度 0 */
    TF_CHECK_INT(mock.out[13], 0x00);
    tf_end();
}

static void test_sub_unsub(void)
{
    static const uint8_t connack_ok[4] = { 0x20, 0x02, 0x00, 0x00 };
    static const uint8_t suback[5] = { 0x90, 0x03, 0x00, 0x01, 0x01 };
    static const uint8_t unsuback[4] = { 0xB0, 0x02, 0x00, 0x02 };
    mqtt_mock_t mock;
    mqtt_config_t cfg;

    mock_reset(&mock);
    default_config(&cfg);
    make_client(&s_client, &mock, &cfg, NULL, NULL);
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    TF_CHECK_INT(mqtt_client_connect(&s_client), IOT_OK);

    tf_begin("mqtt/SUBSCRIBE 报文与 SUBACK");
    mock_clear_out(&mock);
    mock_set_input(&mock, suback, sizeof(suback));
    TF_CHECK_INT(mqtt_client_subscribe(&s_client, "sensor/temp", 1), IOT_OK);
    TF_CHECK_INT(mock.out[0], 0x82);
    TF_CHECK_INT(mock.out[1], 2 + 2 + 11 + 1);
    TF_CHECK_INT(mock.out[2], 0x00);
    TF_CHECK_INT(mock.out[3], 0x01);      /* packet id 从 1 开始 */
    TF_CHECK_INT(mock.out[4], 0x00);
    TF_CHECK_INT(mock.out[5], 11);
    TF_CHECK_MEM(&mock.out[6], "sensor/temp", 11);
    TF_CHECK_INT(mock.out[17], 1);        /* QoS */
    tf_end();

    tf_begin("mqtt/UNSUBSCRIBE 报文与 UNSUBACK");
    mock_clear_out(&mock);
    mock_set_input(&mock, unsuback, sizeof(unsuback));
    TF_CHECK_INT(mqtt_client_unsubscribe(&s_client, "sensor/temp"), IOT_OK);
    TF_CHECK_INT(mock.out[0], 0xA2);
    TF_CHECK_INT(mock.out[2], 0x00);
    TF_CHECK_INT(mock.out[3], 0x02);
    tf_end();

    tf_begin("mqtt/SUBACK 校验失败");
    mock_clear_out(&mock);
    {
        static const uint8_t short_suback[4] = { 0x90, 0x02, 0x00, 0x01 };
        static const uint8_t wrong_suback[5] = { 0x91, 0x03, 0x00, 0x01, 0x00 };
        mock_set_input(&mock, short_suback, sizeof(short_suback));
        TF_CHECK_INT(mqtt_client_subscribe(&s_client, "t", 0), -1);
        mock_set_input(&mock, wrong_suback, sizeof(wrong_suback));
        TF_CHECK_INT(mqtt_client_subscribe(&s_client, "t", 0), -1);
        TF_CHECK_INT(mqtt_client_subscribe(&s_client, "t", 0), -1);
    }
    tf_end();

    tf_begin("mqtt/UNSUBACK 校验失败");
    mock_clear_out(&mock);
    {
        static const uint8_t wrong_unsuback[4] = { 0xB1, 0x02, 0x00, 0x03 };
        mock_set_input(&mock, wrong_unsuback, sizeof(wrong_unsuback));
        TF_CHECK_INT(mqtt_client_unsubscribe(&s_client, "t"), -1);
        mock_reset(&mock);
        TF_CHECK_INT(mqtt_client_unsubscribe(&s_client, "t"), -1);
    }
    tf_end();
}

static void test_publish(void)
{
    static const uint8_t connack_ok[4] = { 0x20, 0x02, 0x00, 0x00 };
    static const uint8_t puback[4] = { 0x40, 0x02, 0x00, 0x01 };
    mqtt_mock_t mock;
    mqtt_config_t cfg;
    mqtt_message_t msg;

    mock_reset(&mock);
    default_config(&cfg);
    make_client(&s_client, &mock, &cfg, NULL, NULL);
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    mqtt_client_connect(&s_client);

    tf_begin("mqtt/PUBLISH QoS0");
    mock_clear_out(&mock);
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.topic, "t");
    msg.payload = (const uint8_t *)"hi";
    msg.payload_len = 2;
    msg.qos = 0;
    TF_CHECK_INT(mqtt_client_publish(&s_client, &msg), IOT_OK);
    TF_CHECK_INT(mock.out_len, 7);
    TF_CHECK_INT(mock.out[0], 0x30);
    TF_CHECK_INT(mock.out[1], 5);
    TF_CHECK_INT(mock.out[2], 0x00);
    TF_CHECK_INT(mock.out[3], 0x01);
    TF_CHECK_MEM(&mock.out[4], "t", 1);
    TF_CHECK_MEM(&mock.out[5], "hi", 2);
    tf_end();

    tf_begin("mqtt/PUBLISH retain 标志");
    mock_clear_out(&mock);
    msg.retain = true;
    TF_CHECK_INT(mqtt_client_publish(&s_client, &msg), IOT_OK);
    TF_CHECK_INT(mock.out[0], 0x31);
    tf_end();

    tf_begin("mqtt/PUBLISH QoS1 与 PUBACK");
    mock_clear_out(&mock);
    mock_set_input(&mock, puback, sizeof(puback));
    msg.retain = false;
    msg.qos = 1;
    TF_CHECK_INT(mqtt_client_publish(&s_client, &msg), IOT_OK);
    TF_CHECK_INT(mock.out[0], 0x32);
    TF_CHECK_INT(mock.out[5], 0x00);      /* packet id */
    TF_CHECK_INT(mock.out[6], 0x01);
    TF_CHECK_MEM(&mock.out[7], "hi", 2);
    tf_end();

    tf_begin("mqtt/PUBLISH QoS 超范围截断为 1");
    mock_clear_out(&mock);
    mock_set_input(&mock, puback, sizeof(puback));
    msg.qos = 2;
    TF_CHECK_INT(mqtt_client_publish(&s_client, &msg), IOT_OK);
    TF_CHECK_INT(mock.out[0], 0x32);
    tf_end();

    tf_begin("mqtt/PUBLISH 空载荷与空主题");
    mock_clear_out(&mock);
    memset(&msg, 0, sizeof(msg));
    msg.qos = 0;
    TF_CHECK_INT(mqtt_client_publish(&s_client, &msg), IOT_OK);
    TF_CHECK_INT(mock.out[0], 0x30);
    TF_CHECK_INT(mock.out[1], 2);          /* 仅主题长度 0 */
    tf_end();

    tf_begin("mqtt/PUBLISH 失败路径");
    mock_clear_out(&mock);
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.topic, "t");
    msg.payload = (const uint8_t *)"x";
    msg.payload_len = 1;
    msg.qos = 1;
    mock_set_input(&mock, puback, 3);      /* plen < 4 */
    TF_CHECK_INT(mqtt_client_publish(&s_client, &msg), -1);
    mock_reset(&mock);                     /* 无应答 */
    TF_CHECK_INT(mqtt_client_publish(&s_client, &msg), -1);
    mock.fail_write = 1;
    TF_CHECK_INT(mqtt_client_publish(&s_client, &msg), -1);
    tf_end();
}

static int s_msg_count;
static char s_topic[256];
static char s_payload[64];
static uint32_t s_payload_len;
static uint8_t s_qos;
static bool s_retain;

static void reset_rec(void)
{
    s_msg_count = 0;
    s_topic[0] = '\0';
    s_payload[0] = '\0';
    s_payload_len = 0;
    s_qos = 0xFF;
    s_retain = false;
}

static void on_message(const mqtt_message_t *message, void *user)
{
    uint32_t n;

    (void)user;
    s_msg_count++;
    strncpy(s_topic, message->topic, sizeof(s_topic) - 1);
    s_topic[sizeof(s_topic) - 1] = '\0';
    n = message->payload_len;
    if (n > sizeof(s_payload) - 1) {
        n = sizeof(s_payload) - 1;
    }
    if (message->payload != NULL) {
        memcpy(s_payload, message->payload, n);
    }
    s_payload[n] = '\0';
    s_payload_len = message->payload_len;
    s_qos = message->qos;
    s_retain = message->retain;
}

static void test_process(void)
{
    static const uint8_t connack_ok[4] = { 0x20, 0x02, 0x00, 0x00 };
    static const uint8_t publish_qos0[] = {
        0x30, 0x0A, 0x00, 0x03, 'a', '/', 'b', 'h', 'e', 'l', 'l', 'o'
    };
    static const uint8_t publish_qos1[] = {
        0x32, 0x06, 0x00, 0x01, 'a', 0x00, 0x07, 'x'
    };
    static const uint8_t publish_retain[] = {
        0x31, 0x04, 0x00, 0x01, 'r', 'y'
    };
    static const uint8_t pingresp[2] = { 0xD0, 0x00 };
    static const uint8_t malformed[] = { 0x30, 0x02, 0x00, 0x05 };
    mqtt_mock_t mock;
    mqtt_config_t cfg;

    tf_begin("mqtt/未连接时 process 不读写");
    mock_reset(&mock);
    default_config(&cfg);
    make_client(&s_client, &mock, &cfg, on_message, NULL);
    reset_rec();
    TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
    TF_CHECK_INT(mock.out_len, 0);
    TF_CHECK_INT(s_msg_count, 0);
    tf_end();

    mock_reset(&mock);
    make_client(&s_client, &mock, &cfg, on_message, NULL);
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    mqtt_client_connect(&s_client);

    tf_begin("mqtt/分发 PUBLISH QoS0");
    mock_clear_out(&mock);
    reset_rec();
    mock_set_input(&mock, publish_qos0, sizeof(publish_qos0));
    TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
    TF_CHECK_INT(s_msg_count, 1);
    TF_CHECK_STR(s_topic, "a/b");
    TF_CHECK_STR(s_payload, "hello");
    TF_CHECK_INT(s_payload_len, 5);
    TF_CHECK_INT(s_qos, 0);
    TF_CHECK(!s_retain);
    TF_CHECK_INT(mock.out_len, 0);
    tf_end();

    tf_begin("mqtt/分发 PUBLISH QoS1 并回 PUBACK");
    mock_clear_out(&mock);
    reset_rec();
    mock_set_input(&mock, publish_qos1, sizeof(publish_qos1));
    TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
    TF_CHECK_INT(s_msg_count, 1);
    TF_CHECK_STR(s_topic, "a");
    TF_CHECK_STR(s_payload, "x");
    TF_CHECK_INT(s_qos, 1);
    TF_CHECK_INT(mock.out_len, 4);
    TF_CHECK_INT(mock.out[0], 0x40);
    TF_CHECK_INT(mock.out[2], 0x00);
    TF_CHECK_INT(mock.out[3], 0x07);
    tf_end();

    tf_begin("mqtt/retain 标志与一次读取多报文");
    mock_clear_out(&mock);
    reset_rec();
    mock_set_input(&mock, publish_retain, sizeof(publish_retain));
    mock_append_input(&mock, publish_qos0, sizeof(publish_qos0));
    TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
    TF_CHECK_INT(s_msg_count, 2);
    tf_end();

    tf_begin("mqtt/PINGRESP 与畸形 PUBLISH 不崩溃");
    mock_clear_out(&mock);
    reset_rec();
    mock_set_input(&mock, pingresp, sizeof(pingresp));
    TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
    TF_CHECK_INT(s_msg_count, 0);

    mock_set_input(&mock, malformed, sizeof(malformed));
    TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
    TF_CHECK_INT(s_msg_count, 1);
    TF_CHECK_STR(s_topic, "");
    TF_CHECK_INT(s_payload_len, 0);
    tf_end();

    tf_begin("mqtt/超长主题被截断到 191 字节");
    {
        static uint8_t big[256];
        uint32_t n = 0;
        uint32_t i;
        big[n++] = 0x30;
        big[n++] = 0xCB;               /* 203 = 0x4B | 0x80 */
        big[n++] = 0x01;
        big[n++] = 0x00;
        big[n++] = 0xC8;               /* topic len 200 */
        for (i = 0; i < 200; i++) {
            big[n++] = 'a';
        }
        big[n++] = 'x';
        mock_clear_out(&mock);
        reset_rec();
        mock_set_input(&mock, big, n);
        TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
        TF_CHECK_INT(s_msg_count, 1);
        TF_CHECK_INT((int)strlen(s_topic), 191);
        TF_CHECK_INT(s_payload_len, 1);
        TF_CHECK_STR(s_payload, "x");
    }
    tf_end();
}

static void test_keepalive_and_reset(void)
{
    static const uint8_t connack_ok[4] = { 0x20, 0x02, 0x00, 0x00 };
    mqtt_mock_t mock;
    mqtt_config_t cfg;
    mqtt_transport_t transport;

    tf_begin("mqtt/keepalive 到点发送 PINGREQ");
    mock_reset(&mock);
    default_config(&cfg);
    cfg.keepalive = 60;
    transport.write = mk_write;
    transport.read = mk_read;
    transport.tick = mk_tick;
    transport.ctx = &mock;
    TF_CHECK_INT(mqtt_client_init(&s_client, &transport, &cfg, NULL, NULL),
                 IOT_OK);

    mock.tick_now = 1000;
    mock_set_input(&mock, connack_ok, sizeof(connack_ok));
    TF_CHECK_INT(mqtt_client_connect(&s_client), IOT_OK);

    mock_clear_out(&mock);
    mock.tick_now = 60000;             /* 距上次 IO 59s，未到 60s */
    TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
    TF_CHECK_INT(mock.out_len, 0);

    mock.tick_now = 61000;             /* 到点 */
    TF_CHECK_INT(mqtt_client_process(&s_client), IOT_OK);
    TF_CHECK_INT(mock.out_len, 2);
    TF_CHECK_INT(mock.out[0], 0xC0);
    TF_CHECK_INT(mock.out[1], 0x00);
    tf_end();

    tf_begin("mqtt/断开连接与重置");
    mock_clear_out(&mock);
    TF_CHECK_INT(mqtt_client_disconnect(&s_client), IOT_OK);
    TF_CHECK_INT(mock.out_len, 2);
    TF_CHECK_INT(mock.out[0], 0xE0);
    TF_CHECK(!mqtt_client_is_connected(&s_client));

    mock_clear_out(&mock);
    TF_CHECK_INT(mqtt_client_disconnect(&s_client), IOT_OK);
    TF_CHECK_INT(mock.out_len, 0);      /* 已断开不重复发送 */

    s_client.connected = true;
    s_client.rx_len = 9;
    mqtt_client_reset(&s_client);
    TF_CHECK(!mqtt_client_is_connected(&s_client));
    TF_CHECK_INT(s_client.rx_len, 0);
    tf_end();
}

void ut_mqtt(void)
{
    test_init();
    test_connect();
    test_sub_unsub();
    test_publish();
    test_process();
    test_keepalive_and_reset();
}
