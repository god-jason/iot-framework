/*
 * MQTT 3.1.1 客户端（软件实现）。
 * 支持 QoS0 / QoS1、订阅、发布、保活心跳与消息分发。
 */
#include "mqtt.h"

#include <string.h>

/* ---------- 字节流读取 ---------- */
static int mqtt_read_exact(mqtt_client_t *c, uint8_t *buf, uint32_t len,
                           uint32_t timeout)
{
    uint32_t got = 0;
    while (got < len) {
        int n = c->transport.read(c->transport.ctx, buf + got, len - got, timeout);
        if (n <= 0) {
            return -1;   /* 超时 / 关闭 / 错误 */
        }
        got += (uint32_t)n;
    }
    return 0;
}

/* 读取一个完整 MQTT 报文到 out，返回 0 成功；超时/错误返回 -1 */
static int mqtt_read_packet(mqtt_client_t *c, uint8_t *out, uint32_t cap,
                            uint32_t *plen, uint32_t timeout)
{
    uint8_t first;
    uint32_t remlen = 0;
    uint32_t i;
    uint32_t multiplier = 1;
    uint32_t rl_bytes = 0;

    if (mqtt_read_exact(c, &first, 1, timeout) != 0) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        uint8_t b;
        if (mqtt_read_exact(c, &b, 1, timeout) != 0) {
            return -1;
        }
        remlen += (uint32_t)(b & 0x7F) * multiplier;
        rl_bytes++;
        if ((b & 0x80) == 0) {
            break;
        }
        multiplier *= 128;
    }
    if (cap < 1u + rl_bytes + remlen) {
        return -1;
    }
    out[0] = first;
    if (mqtt_read_exact(c, out + 1 + rl_bytes, remlen, timeout) != 0) {
        return -1;
    }
    /* 回写剩余的 length 字节（1..4B） */
    {
        uint32_t t = remlen;
        for (i = 0; i < rl_bytes; i++) {
            uint8_t b = (uint8_t)(t & 0x7F);
            t >>= 7;
            if (i + 1 < rl_bytes) {
                b |= 0x80;
            }
            out[1 + i] = b;
        }
    }
    *plen = 1u + rl_bytes + remlen;
    return 0;
}

/* ---------- 编码 ---------- */
static uint32_t mqtt_remlen_encode(uint8_t *buf, uint32_t v)
{
    uint32_t n = 0;
    do {
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;
        if (v > 0) {
            b |= 0x80;
        }
        buf[n++] = b;
    } while (v > 0);
    return n;
}

static uint32_t mqtt_encode_str(uint8_t *p, const char *s)
{
    uint16_t l = (uint16_t)strlen(s);
    p[0] = (uint8_t)(l >> 8);
    p[1] = (uint8_t)(l & 0xFF);
    memcpy(p + 2, s, l);
    return 2u + l;
}

/* 在 pkt[1..] 组装 remaining length + payload，返回完整报文长度 */
static uint32_t mqtt_finish(uint8_t *pkt, uint8_t type, const uint8_t *payload,
                            uint32_t payload_len)
{
    uint32_t rl = mqtt_remlen_encode(&pkt[1], payload_len);
    memcpy(&pkt[1 + rl], payload, payload_len);
    pkt[0] = type;
    return 1u + rl + payload_len;
}

/* ---------- 消息分发 ---------- */
static void mqtt_handle_packet(mqtt_client_t *c, const uint8_t *pkt, uint32_t len)
{
    uint8_t type = (uint8_t)(pkt[0] & 0xF0);
    uint32_t off = 1;

    /* 跳过 remaining length 字段 */
    while (off < len && (pkt[off] & 0x80)) {
        off++;
    }
    off++;   /* 指向 payload 起始 */

    if (type == 0x30 || type == 0x31 || type == 0x32) { /* PUBLISH */
        mqtt_message_t msg;
        uint16_t topic_len = 0;
        uint8_t msg_qos = (uint8_t)((pkt[0] >> 1) & 0x03);
        uint16_t pid = 0;

        memset(&msg, 0, sizeof(msg));
        if (off + 2 <= len) {
            topic_len = (uint16_t)((pkt[off] << 8) | pkt[off + 1]);
            off += 2;
            if (off + topic_len <= len) {
                uint32_t cap = sizeof(msg.topic) - 1;
                uint32_t n = (topic_len < cap) ? topic_len : cap;
                memcpy(msg.topic, &pkt[off], n);
                msg.topic[n] = '\0';
                off += topic_len;
            }
        }
        if (msg_qos > 0 && off + 2 <= len) {
            pid = (uint16_t)((pkt[off] << 8) | pkt[off + 1]);
            off += 2;
        }
        msg.payload = (off < len) ? &pkt[off] : NULL;
        msg.payload_len = (off < len) ? (len - off) : 0;
        msg.qos = (uint8_t)msg_qos;
        msg.retain = (pkt[0] & 0x01) != 0;

        if (msg_qos == 1) {
            uint8_t ack[4] = { 0x40, 0x02,
                               (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF) };
            c->transport.write(c->transport.ctx, ack, sizeof(ack));
        }
        if (c->on_message != NULL) {
            c->on_message(&msg, c->user);
        }
    }
    /* 其它报文（CONNACK/PUBACK/SUBACK/UNSUBACK/PINGRESP）由阻塞调用消耗或忽略 */
}

/* ---------- 客户端 API ---------- */
int mqtt_client_init(mqtt_client_t *client, const mqtt_transport_t *transport,
                     const mqtt_config_t *config,
                     mqtt_message_fn on_message, void *user)
{
    if (client == NULL || transport == NULL || config == NULL ||
        transport->write == NULL || transport->read == NULL) {
        return -1;
    }
    memset(client, 0, sizeof(*client));
    client->transport = *transport;
    client->config = *config;
    client->on_message = on_message;
    client->user = user;
    client->next_packet_id = 1;
    return IOT_OK;
}

static uint32_t mqtt_now(const mqtt_client_t *c)
{
    return (c->transport.tick != NULL)
           ? c->transport.tick(c->transport.ctx) : 0;
}

int mqtt_client_connect(mqtt_client_t *client)
{
    uint8_t var[512];
    uint8_t pkt[512];
    uint8_t flags = 0;
    uint32_t v = 0;
    uint32_t len;
    uint32_t plen;
    uint8_t resp[8];

    /* variable header: 协议名 + 版本 + 连接标志 + keepalive */
    var[v++] = 0;
    var[v++] = 4;
    memcpy(&var[v], "MQTT", 4);
    v += 4;
    var[v++] = 4;   /* protocol level 3.1.1 */
    if (client->config.clean_session) {
        flags |= 0x02;
    }
    if (client->config.username[0] != '\0') {
        flags |= 0x80;
    }
    if (client->config.password[0] != '\0') {
        flags |= 0x40;
    }
    var[v++] = flags;
    var[v++] = (uint8_t)(client->config.keepalive >> 8);
    var[v++] = (uint8_t)(client->config.keepalive & 0xFF);

    v += mqtt_encode_str(&var[v], client->config.client_id);
    if (client->config.username[0] != '\0') {
        v += mqtt_encode_str(&var[v], client->config.username);
    }
    if (client->config.password[0] != '\0') {
        v += mqtt_encode_str(&var[v], client->config.password);
    }

    len = mqtt_finish(pkt, 0x10, var, v);
    if (client->transport.write(client->transport.ctx, pkt, len) != (int)len) {
        return -1;
    }

    if (mqtt_read_packet(client, resp, sizeof(resp), &plen, 10000) != 0) {
        return -1;
    }
    if (plen < 4 || resp[0] != 0x20 || resp[3] != 0) {
        return -1;
    }
    client->connected = true;
    client->last_io_ms = mqtt_now(client);
    return IOT_OK;
}

int mqtt_client_disconnect(mqtt_client_t *client)
{
    uint8_t pkt[2] = { 0xE0, 0x00 };
    if (client->connected) {
        client->transport.write(client->transport.ctx, pkt, 2);
    }
    client->connected = false;
    return IOT_OK;
}

int mqtt_client_subscribe(mqtt_client_t *client, const char *topic, uint8_t qos)
{
    uint8_t var[256];
    uint8_t pkt[256];
    uint8_t resp[8];
    uint16_t pid;
    uint32_t v = 0;
    uint32_t len;
    uint32_t plen;

    if (!client->connected) {
        return -1;
    }
    pid = client->next_packet_id++;
    var[v++] = (uint8_t)(pid >> 8);
    var[v++] = (uint8_t)(pid & 0xFF);
    v += mqtt_encode_str(&var[v], topic);
    var[v++] = qos;

    len = mqtt_finish(pkt, 0x82, var, v);
    if (client->transport.write(client->transport.ctx, pkt, len) != (int)len) {
        return -1;
    }
    if (mqtt_read_packet(client, resp, sizeof(resp), &plen, 5000) != 0) {
        return -1;
    }
    if (plen < 5 || resp[0] != 0x90) {
        return -1;
    }
    client->last_io_ms = mqtt_now(client);
    return IOT_OK;
}

int mqtt_client_unsubscribe(mqtt_client_t *client, const char *topic)
{
    uint8_t var[256];
    uint8_t pkt[256];
    uint8_t resp[8];
    uint16_t pid;
    uint32_t v = 0;
    uint32_t len;
    uint32_t plen;

    if (!client->connected) {
        return -1;
    }
    pid = client->next_packet_id++;
    var[v++] = (uint8_t)(pid >> 8);
    var[v++] = (uint8_t)(pid & 0xFF);
    v += mqtt_encode_str(&var[v], topic);

    len = mqtt_finish(pkt, 0xA2, var, v);
    if (client->transport.write(client->transport.ctx, pkt, len) != (int)len) {
        return -1;
    }
    if (mqtt_read_packet(client, resp, sizeof(resp), &plen, 5000) != 0) {
        return -1;
    }
    if (plen < 4 || resp[0] != 0xB0) {
        return -1;
    }
    client->last_io_ms = mqtt_now(client);
    return IOT_OK;
}

int mqtt_client_publish(mqtt_client_t *client, const mqtt_message_t *message)
{
    uint8_t var[1024];
    uint8_t pkt[1100];
    uint8_t resp[8];
    uint16_t pid = 0;
    uint8_t type;
    uint32_t v = 0;
    uint32_t len;
    uint32_t plen;
    uint8_t qos;

    if (!client->connected) {
        return -1;
    }
    qos = message->qos > 1 ? 1 : message->qos;
    v += mqtt_encode_str(&var[v], message->topic);
    if (qos == 1) {
        pid = client->next_packet_id++;
        var[v++] = (uint8_t)(pid >> 8);
        var[v++] = (uint8_t)(pid & 0xFF);
    }
    if (message->payload != NULL && message->payload_len > 0) {
        memcpy(&var[v], message->payload, message->payload_len);
        v += message->payload_len;
    }

    type = (uint8_t)(0x30 | (qos << 1) | (message->retain ? 0x01 : 0x00));
    len = mqtt_finish(pkt, type, var, v);
    if (client->transport.write(client->transport.ctx, pkt, len) != (int)len) {
        return -1;
    }

    if (qos == 1) {
        if (mqtt_read_packet(client, resp, sizeof(resp), &plen, 5000) != 0) {
            return -1;
        }
        if (plen < 4 || resp[0] != 0x40) {
            return -1;
        }
    }
    client->last_io_ms = mqtt_now(client);
    return IOT_OK;
}

int mqtt_client_process(mqtt_client_t *client)
{
    uint8_t pkt[1100];
    uint32_t plen;

    if (!client->connected) {
        return IOT_OK;
    }

    /* 泵取并分发到达的完整报文（超时 0，无数据即返回） */
    while (mqtt_read_packet(client, pkt, sizeof(pkt), &plen, 0) == 0) {
        mqtt_handle_packet(client, pkt, plen);
    }

    /* 保活：到达 keepalive 周期则发送 PINGREQ */
    if (client->transport.tick != NULL && client->config.keepalive > 0) {
        uint32_t now = mqtt_now(client);
        if (now - client->last_io_ms >= (uint32_t)client->config.keepalive * 1000u) {
            uint8_t ping[2] = { 0xC0, 0x00 };
            client->transport.write(client->transport.ctx, ping, 2);
            client->last_io_ms = now;
        }
    }
    return IOT_OK;
}

bool mqtt_client_is_connected(const mqtt_client_t *client)
{
    return client->connected;
}

void mqtt_client_reset(mqtt_client_t *client)
{
    client->connected = false;
    client->rx_len = 0;
}