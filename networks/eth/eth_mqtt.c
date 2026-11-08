/*
 * MQTT 客户端实现：基于系统 socket，通过 Mongoose MQTT 客户端完成。
 * 使用共享的 mg_mgr 事件循环，iot_mqtt_process() 负责驱动收包。
 */
#include "iot_mqtt.h"
#include "eth_net.h"

#if defined(IOT_NETWORK_ETH)

#include <stdio.h>
#include <string.h>

#define ETH_MQTT_URL_MAX       320
#define ETH_MQTT_CONNECT_MS    10000U
#define ETH_MQTT_POLL_MS       5

typedef struct {
    struct mg_connection *conn;
    bool opened;
    bool failed;
    bool connected;
    iot_mqtt_message_cb_t callback;
    void *user_data;
} eth_mqtt_state_t;

static eth_mqtt_state_t s_mqtt;

static void eth_mqtt_deliver(struct mg_mqtt_message *message)
{
    iot_mqtt_message_t msg;
    size_t len;

    if (s_mqtt.callback == NULL) {
        return;
    }

    memset(&msg, 0, sizeof(msg));

    len = message->topic.len;
    if (len >= sizeof(msg.topic)) {
        len = sizeof(msg.topic) - 1;
    }
    if (message->topic.buf != NULL) {
        memcpy(msg.topic, message->topic.buf, len);
    }
    msg.topic[len] = '\0';

    msg.payload = (const uint8_t *)message->data.buf;
    msg.payload_len = (uint32_t)message->data.len;
    msg.qos = message->qos;
    msg.retain = (message->dgram.len > 0) &&
                 ((message->dgram.buf[0] & 0x01) != 0);

    s_mqtt.callback(&msg, s_mqtt.user_data);
}

static void eth_mqtt_fn(struct mg_connection *c, int ev, void *ev_data)
{
    switch (ev) {
    case MG_EV_MQTT_OPEN: {
        int code = *(int *)ev_data;

        s_mqtt.opened = true;
        s_mqtt.connected = (code == 0);
        if (code != 0) {
            s_mqtt.failed = true;
        }
        break;
    }

    case MG_EV_MQTT_MSG:
        eth_mqtt_deliver((struct mg_mqtt_message *)ev_data);
        break;

    case MG_EV_ERROR:
        s_mqtt.failed = true;
        s_mqtt.opened = true;
        s_mqtt.connected = false;
        c->is_closing = 1;
        break;

    case MG_EV_CLOSE:
        s_mqtt.conn = NULL;
        s_mqtt.connected = false;
        s_mqtt.opened = true;       /* 解除 connect 等待 */
        break;

    default:
        break;
    }
}

/* ---------------- iot_mqtt 接口实现 ---------------- */

hal_status_t iot_mqtt_init(iot_mqtt_message_cb_t callback, void *user_data)
{
    (void)eth_net_mgr();

    s_mqtt.callback = callback;
    s_mqtt.user_data = user_data;
    return HAL_OK;
}

hal_status_t iot_mqtt_deinit(void)
{
    if (s_mqtt.conn != NULL) {
        mg_mqtt_disconnect(s_mqtt.conn, NULL);
        s_mqtt.conn->is_closing = 1;
        eth_net_poll(0);
        s_mqtt.conn = NULL;
    }
    s_mqtt.connected = false;
    s_mqtt.callback = NULL;
    s_mqtt.user_data = NULL;
    return HAL_OK;
}

hal_status_t iot_mqtt_connect(const iot_mqtt_config_t *config)
{
    struct mg_mqtt_opts opts;
    char url[ETH_MQTT_URL_MAX];
    uint64_t start;

    if (config == NULL || config->host == NULL || config->host[0] == '\0') {
        return HAL_INVALID_PARAM;
    }
    if (config->ssl) {
        return HAL_NOT_SUPPORTED;      /* 未启用 TLS 后端 */
    }
    if (snprintf(url, sizeof(url), "mqtt://%s:%u",
                 config->host, (unsigned)config->port) >= (int)sizeof(url)) {
        return HAL_INVALID_PARAM;
    }

    memset(&opts, 0, sizeof(opts));
    opts.client_id = mg_str(config->client_id);
    opts.user = mg_str(config->username);
    opts.pass = mg_str(config->password);
    opts.keepalive = config->keepalive;
    opts.clean = config->clean_session;

    s_mqtt.opened = false;
    s_mqtt.failed = false;
    s_mqtt.connected = false;

    s_mqtt.conn = mg_mqtt_connect(eth_net_mgr(), url, &opts, eth_mqtt_fn, NULL);
    if (s_mqtt.conn == NULL) {
        return HAL_IO_ERROR;
    }

    start = mg_millis();
    while (!s_mqtt.opened) {
        if (mg_millis() - start >= ETH_MQTT_CONNECT_MS) {
            break;
        }
        eth_net_poll(ETH_MQTT_POLL_MS);
    }

    if (s_mqtt.failed || !s_mqtt.connected) {
        if (s_mqtt.conn != NULL) {
            s_mqtt.conn->is_closing = 1;
            eth_net_poll(0);
            s_mqtt.conn = NULL;
        }
        return HAL_IO_ERROR;
    }
    return HAL_OK;
}

hal_status_t iot_mqtt_disconnect(void)
{
    if (s_mqtt.conn == NULL) {
        return HAL_NOT_READY;
    }
    mg_mqtt_disconnect(s_mqtt.conn, NULL);
    s_mqtt.conn->is_closing = 1;
    eth_net_poll(0);
    s_mqtt.conn = NULL;
    s_mqtt.connected = false;
    return HAL_OK;
}

hal_status_t iot_mqtt_subscribe(const char *topic, uint8_t qos)
{
    struct mg_mqtt_opts opts;

    if (topic == NULL || topic[0] == '\0') {
        return HAL_INVALID_PARAM;
    }
    if (s_mqtt.conn == NULL || !s_mqtt.connected) {
        return HAL_NOT_READY;
    }

    memset(&opts, 0, sizeof(opts));
    opts.topic = mg_str(topic);
    opts.qos = qos;
    mg_mqtt_sub(s_mqtt.conn, &opts);
    return HAL_OK;
}

hal_status_t iot_mqtt_unsubscribe(const char *topic)
{
    struct mg_mqtt_opts opts;

    if (topic == NULL || topic[0] == '\0') {
        return HAL_INVALID_PARAM;
    }
    if (s_mqtt.conn == NULL || !s_mqtt.connected) {
        return HAL_NOT_READY;
    }

    memset(&opts, 0, sizeof(opts));
    opts.topic = mg_str(topic);
    mg_mqtt_unsub(s_mqtt.conn, &opts);
    return HAL_OK;
}

hal_status_t iot_mqtt_publish(const iot_mqtt_message_t *message)
{
    struct mg_mqtt_opts opts;

    if (message == NULL || message->topic[0] == '\0') {
        return HAL_INVALID_PARAM;
    }
    if (s_mqtt.conn == NULL || !s_mqtt.connected) {
        return HAL_NOT_READY;
    }

    memset(&opts, 0, sizeof(opts));
    opts.topic = mg_str(message->topic);
    opts.message = mg_str_n((const char *)message->payload, message->payload_len);
    opts.qos = message->qos;
    opts.retain = message->retain;
    (void)mg_mqtt_pub(s_mqtt.conn, &opts);
    return HAL_OK;
}

hal_status_t iot_mqtt_process(void)
{
    eth_net_poll(0);
    return HAL_OK;
}

bool iot_mqtt_is_connected(void)
{
    return s_mqtt.connected && s_mqtt.conn != NULL;
}

#endif /* IOT_NETWORK_ETH */
