/* ETH MQTT 功能测试：本机 Mongoose MQTT 服务端 + iot_mqtt 客户端。 */
#include "test.h"
#include "iot_mqtt.h"
#include "eth_net.h"

#include <string.h>

static bool s_received;
static char s_topic[IOT_MQTT_TOPIC_MAX];
static char s_payload[32];

static void mqtt_on_message(const iot_mqtt_message_t *message, void *user_data)
{
    uint32_t n;

    (void)user_data;

    s_received = true;
    strncpy(s_topic, message->topic, sizeof(s_topic) - 1U);
    s_topic[sizeof(s_topic) - 1U] = '\0';

    n = (message->payload_len < sizeof(s_payload) - 1U)
        ? message->payload_len : (uint32_t)(sizeof(s_payload) - 1U);
    if (message->payload != NULL && n > 0U) {
        memcpy(s_payload, message->payload, n);
    }
    s_payload[n] = '\0';
}

/* 最小 MQTT 服务端：CONNACK / SUBACK / UNSUBACK / PUBLISH 回显 / PINGRESP */
static void mqtt_server_fn(struct mg_connection *c, int ev, void *ev_data)
{
    struct mg_mqtt_message *message;
    uint8_t id[2];
    uint8_t code;

    if (ev != MG_EV_MQTT_CMD) {
        return;
    }

    message = (struct mg_mqtt_message *)ev_data;
    switch (message->cmd) {
    case MQTT_CMD_CONNECT:
        mg_mqtt_send_header(c, MQTT_CMD_CONNACK, 0, 2);
        code = 0U;
        (void)mg_send(c, &code, 1);
        (void)mg_send(c, &code, 1);
        break;

    case MQTT_CMD_SUBSCRIBE:
        id[0] = (uint8_t)(message->id >> 8);
        id[1] = (uint8_t)(message->id & 0xFFU);
        mg_mqtt_send_header(c, MQTT_CMD_SUBACK, 0, 3);
        (void)mg_send(c, id, 2);
        code = 0U;
        (void)mg_send(c, &code, 1);
        break;

    case MQTT_CMD_UNSUBSCRIBE:
        id[0] = (uint8_t)(message->id >> 8);
        id[1] = (uint8_t)(message->id & 0xFFU);
        mg_mqtt_send_header(c, MQTT_CMD_UNSUBACK, 0, 2);
        (void)mg_send(c, id, 2);
        break;

    case MQTT_CMD_PUBLISH:
        (void)mg_send(c, message->dgram.buf, message->dgram.len);
        break;

    case MQTT_CMD_PINGREQ:
        mg_mqtt_pong(c);
        break;

    default:
        break;
    }
}

void ut_eth_mqtt(void)
{
    struct mg_connection *server;
    iot_mqtt_config_t config;
    iot_mqtt_message_t message;
    int i;

    tf_begin("eth_mqtt/连接、订阅与发布");
    s_received = false;
    s_topic[0] = '\0';
    s_payload[0] = '\0';

    TF_CHECK_INT(iot_mqtt_init(mqtt_on_message, NULL), HAL_OK);
    server = mg_mqtt_listen(eth_net_mgr(), "tcp://127.0.0.1:18830",
                            mqtt_server_fn, NULL);
    TF_CHECK(server != NULL);

    memset(&config, 0, sizeof(config));
    config.host = "127.0.0.1";
    config.port = 18830U;
    config.client_id = "eth-test";
    config.keepalive = 0U;
    config.clean_session = true;
    TF_CHECK_INT(iot_mqtt_connect(&config), HAL_OK);
    TF_CHECK(iot_mqtt_is_connected());

    TF_CHECK_INT(iot_mqtt_subscribe("test/topic", 0U), HAL_OK);
    for (i = 0; i < 20; i++) {
        eth_net_poll(1);
    }

    memset(&message, 0, sizeof(message));
    strncpy(message.topic, "test/topic", sizeof(message.topic) - 1U);
    message.payload = (const uint8_t *)"hello-mqtt";
    message.payload_len = 10U;
    message.qos = 0U;
    message.retain = false;
    TF_CHECK_INT(iot_mqtt_publish(&message), HAL_OK);

    for (i = 0; i < 100 && !s_received; i++) {
        (void)iot_mqtt_process();
        eth_net_poll(1);
    }

    TF_CHECK(s_received);
    TF_CHECK_STR(s_topic, "test/topic");
    TF_CHECK_STR(s_payload, "hello-mqtt");

    TF_CHECK_INT(iot_mqtt_disconnect(), HAL_OK);
    TF_CHECK_INT(iot_mqtt_deinit(), HAL_OK);
    server->is_closing = 1;
    eth_net_poll(0);
    tf_end();
}
