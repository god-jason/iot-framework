/*
 * mqtt.c - MQTT 3.1.1 最小客户端
 *
 * 收发都基于 net_stream_*（带缓冲）；入站 PUBLISH 通过回调上抛，
 * QoS1 会自动回 PUBACK；QoS1 发布等待 PUBACK。
 */

#include "mqtt.h"

#if NET_ENABLE

#include <stdio.h>
#include <string.h>

#define MQTT_BUF_MAX        384U

#define MQTT_CONNECT        0x10U
#define MQTT_CONNACK        0x20U
#define MQTT_PUBLISH        0x30U
#define MQTT_PUBACK         0x40U
#define MQTT_SUBSCRIBE      0x82U
#define MQTT_SUBACK         0x90U
#define MQTT_PINGREQ        0xC0U
#define MQTT_PINGRESP       0xD0U
#define MQTT_DISCONNECT     0xE0U

/* 追加 UTF-8 字符串（2 字节长度前缀），越界返回 false */
static bool mqtt_put_str(uint8_t *buf, uint32_t cap, uint32_t *off,
                         const char *s)
{
    size_t len = strlen(s);

    if (len > 0xFFFFU || *off + 2U + len > cap) {
        return false;
    }
    buf[(*off)++] = (uint8_t)(len >> 8);
    buf[(*off)++] = (uint8_t)(len & 0xFFU);
    memcpy(&buf[*off], s, len);
    *off += (uint32_t)len;
    return true;
}

static int mqtt_read_full(mqtt_client_t *c, uint8_t *buf, uint32_t len,
                          uint32_t timeout_ms)
{
    uint32_t got = 0U;

    c->stream.timeout_ms = timeout_ms;
    while (got < len) {
        int n = net_stream_read(&c->stream, &buf[got], len - got);

        if (n > 0) {
            got += (uint32_t)n;
            continue;
        }
        if (n == NET_ERR_CLOSED) {
            c->connected = false;
        }
        return (n == 0) ? NET_ERR_TIMEOUT : n;
    }
    return NET_OK;
}

/* 发送一个完整报文：type + 变长 remaining + body */
static int mqtt_send(mqtt_client_t *c, uint8_t type, const uint8_t *body,
                     uint32_t len)
{
    uint8_t hdr[5];
    uint32_t n = 1U;
    uint32_t v = len;
    int rc;

    hdr[0] = type;
    do {
        uint8_t d = (uint8_t)(v % 128U);

        v /= 128U;
        if (v != 0U) {
            d |= 0x80U;
        }
        hdr[n++] = d;
    } while (v != 0U);

    rc = net_stream_write(&c->stream, hdr, n);
    if (rc < 0) {
        return rc;
    }
    if (len != 0U) {
        rc = net_stream_write(&c->stream, body, len);
        if (rc < 0) {
            return rc;
        }
    }
    return net_stream_flush(&c->stream);
}

static int mqtt_rx(mqtt_client_t *c, uint8_t *type, uint8_t *body,
                   uint32_t cap, uint32_t *body_len, uint32_t timeout_ms)
{
    uint32_t remaining = 0U;
    uint32_t mult = 1U;
    int i;
    int rc;
    uint8_t b;

    rc = mqtt_read_full(c, &b, 1U, timeout_ms);
    if (rc != NET_OK) {
        return rc;
    }
    *type = b;

    for (i = 0; i < 4; i++) {
        rc = mqtt_read_full(c, &b, 1U, timeout_ms);
        if (rc != NET_OK) {
            return rc;
        }
        remaining += (uint32_t)(b & 0x7FU) * mult;
        mult *= 128U;
        if ((b & 0x80U) == 0U) {
            break;
        }
    }
    if (i == 4) {
        return NET_ERR_IO;
    }
    if (remaining > cap) {
        return NET_ERR_NOMEM;
    }
    if (remaining != 0U) {
        rc = mqtt_read_full(c, body, remaining, timeout_ms);
        if (rc != NET_OK) {
            return rc;
        }
    }
    *body_len = remaining;
    return NET_OK;
}

/* 处理入站 PUBLISH / PINGRESP；其他类型忽略 */
static int mqtt_dispatch(mqtt_client_t *c, uint8_t type, const uint8_t *body,
                         uint32_t len)
{
    uint8_t kind = (uint8_t)(type & 0xF0U);

    if (kind == MQTT_PUBLISH) {
        uint16_t tlen;
        uint32_t off;
        uint32_t n;
        uint8_t qos = (uint8_t)((type >> 1) & 0x03U);
        char topic[MQTT_TOPIC_MAX];

        if (len < 2U) {
            return NET_ERR_IO;
        }
        tlen = (uint16_t)(((uint16_t)body[0] << 8) | body[1]);
        if (tlen == 0U || 2U + (uint32_t)tlen > len) {
            return NET_ERR_IO;
        }
        n = (tlen < (uint16_t)(MQTT_TOPIC_MAX - 1U)) ? tlen
                                                     : (uint16_t)(MQTT_TOPIC_MAX - 1U);
        memcpy(topic, &body[2], n);
        topic[n] = '\0';
        off = 2U + tlen;

        if (qos > 0U) {
            uint8_t ack[2];

            if (off + 2U > len) {
                return NET_ERR_IO;
            }
            ack[0] = body[off];
            ack[1] = body[off + 1U];
            off += 2U;
            if (c->cb != NULL) {
                c->cb(topic, &body[off], len - off, c->user);
            }
            if (qos == 1U) {
                return mqtt_send(c, MQTT_PUBACK, ack, 2U);
            }
            return NET_OK;
        }
        if (c->cb != NULL) {
            c->cb(topic, &body[off], len - off, c->user);
        }
        return NET_OK;
    }
    return NET_OK;
}

/* 等待指定类型报文（期间处理 PUBLISH 等） */
static int mqtt_wait(mqtt_client_t *c, uint8_t expect, uint8_t *body,
                     uint32_t cap, uint32_t *body_len, uint32_t timeout_ms)
{
    for (;;) {
        uint8_t type;
        uint32_t len = 0U;
        int rc = mqtt_rx(c, &type, body, cap, &len, timeout_ms);

        if (rc != NET_OK) {
            return rc;
        }
        if (type == expect) {
            if (body_len != NULL) {
                *body_len = len;
            }
            return NET_OK;
        }
        rc = mqtt_dispatch(c, type, body, len);
        if (rc != NET_OK) {
            return rc;
        }
    }
}

int mqtt_client_init(mqtt_client_t *c, mqtt_message_cb cb, void *user)
{
    if (c == NULL) {
        return NET_ERR_PARAM;
    }
    memset(c, 0, sizeof(*c));
    c->cb = cb;
    c->user = user;
    c->next_pid = 1U;
    return NET_OK;
}

int mqtt_connect(mqtt_client_t *c, const char *host, uint16_t port,
                 const mqtt_options_t *opt, uint32_t timeout_ms)
{
    uint8_t body[MQTT_BUF_MAX];
    uint32_t n = 0U;
    uint8_t flags = 0U;
    uint16_t keepalive;
    uint8_t ack[4];
    uint32_t ack_len = 0U;
    int rc;

    if (c == NULL || host == NULL || opt == NULL ||
        opt->client_id == NULL || opt->client_id[0] == '\0') {
        return NET_ERR_PARAM;
    }

    rc = net_open(&c->sock, NET_TCP, host, port, timeout_ms);
    if (rc != NET_OK) {
        return rc;
    }
    rc = net_stream_init(&c->stream, &c->sock, timeout_ms);
    if (rc != NET_OK) {
        (void)net_close(&c->sock);
        return rc;
    }

    keepalive = (opt->keepalive_s != 0U) ? opt->keepalive_s : 60U;
    if (opt->clean_session) {
        flags |= 0x02U;
    }
    if (opt->username != NULL && opt->username[0] != '\0') {
        flags |= 0x80U;
    }
    if (opt->password != NULL && opt->password[0] != '\0') {
        flags |= 0x40U;
    }

    if (!mqtt_put_str(body, sizeof(body), &n, "MQTT") ||
        n + 4U > sizeof(body)) {
        (void)net_stream_close(&c->stream);
        return NET_ERR_PARAM;
    }
    body[n++] = 4U;                 /* protocol level */
    body[n++] = flags;
    body[n++] = (uint8_t)(keepalive >> 8);
    body[n++] = (uint8_t)(keepalive & 0xFFU);
    if (!mqtt_put_str(body, sizeof(body), &n, opt->client_id)) {
        (void)net_stream_close(&c->stream);
        return NET_ERR_PARAM;
    }
    if ((flags & 0x80U) != 0U &&
        !mqtt_put_str(body, sizeof(body), &n, opt->username)) {
        (void)net_stream_close(&c->stream);
        return NET_ERR_PARAM;
    }
    if ((flags & 0x40U) != 0U &&
        !mqtt_put_str(body, sizeof(body), &n, opt->password)) {
        (void)net_stream_close(&c->stream);
        return NET_ERR_PARAM;
    }

    rc = mqtt_send(c, MQTT_CONNECT, body, n);
    if (rc < 0) {
        (void)net_stream_close(&c->stream);
        return rc;
    }
    rc = mqtt_wait(c, MQTT_CONNACK, ack, sizeof(ack), &ack_len, timeout_ms);
    if (rc != NET_OK) {
        (void)net_stream_close(&c->stream);
        return rc;
    }
    if (ack_len < 2U || ack[1] != 0U) {
        (void)net_stream_close(&c->stream);
        return NET_ERR_IO;
    }
    c->connected = true;
    c->next_pid = 1U;
    return NET_OK;
}

int mqtt_publish(mqtt_client_t *c, const char *topic, const void *payload,
                 uint32_t len, uint8_t qos, bool retain)
{
    uint8_t body[MQTT_BUF_MAX];
    uint32_t n = 0U;
    uint8_t type;
    uint16_t pid = 0U;
    int rc;

    if (c == NULL || !c->connected || topic == NULL ||
        (payload == NULL && len != 0U) || qos > 1U) {
        return NET_ERR_PARAM;
    }
    if (!mqtt_put_str(body, sizeof(body), &n, topic)) {
        return NET_ERR_PARAM;
    }
    if (qos == 1U) {
        pid = c->next_pid++;
        if (pid == 0U) {
            pid = c->next_pid++;
        }
        if (n + 2U > sizeof(body)) {
            return NET_ERR_PARAM;
        }
        body[n++] = (uint8_t)(pid >> 8);
        body[n++] = (uint8_t)(pid & 0xFFU);
    }
    if (n + len > sizeof(body)) {
        return NET_ERR_PARAM;
    }
    if (len != 0U) {
        memcpy(&body[n], payload, len);
        n += len;
    }

    type = (uint8_t)(MQTT_PUBLISH | ((qos & 1U) << 1) |
                     (retain ? 0x01U : 0x00U));
    rc = mqtt_send(c, type, body, n);
    if (rc < 0) {
        return rc;
    }
    if (qos == 1U) {
        uint8_t ack[4];
        uint32_t ack_len = 0U;

        rc = mqtt_wait(c, MQTT_PUBACK, ack, sizeof(ack), &ack_len, 5000U);
        if (rc != NET_OK) {
            return rc;
        }
    }
    return NET_OK;
}

int mqtt_subscribe(mqtt_client_t *c, const char *topic, uint8_t qos)
{
    uint8_t body[MQTT_BUF_MAX];
    uint8_t ack[8];
    uint32_t n = 0U;
    uint32_t ack_len = 0U;
    uint16_t pid;
    int rc;

    if (c == NULL || !c->connected || topic == NULL || qos > 1U) {
        return NET_ERR_PARAM;
    }
    pid = c->next_pid++;
    if (pid == 0U) {
        pid = c->next_pid++;
    }
    body[n++] = (uint8_t)(pid >> 8);
    body[n++] = (uint8_t)(pid & 0xFFU);
    if (!mqtt_put_str(body, sizeof(body), &n, topic)) {
        return NET_ERR_PARAM;
    }
    body[n++] = qos;

    rc = mqtt_send(c, MQTT_SUBSCRIBE, body, n);
    if (rc < 0) {
        return rc;
    }
    rc = mqtt_wait(c, MQTT_SUBACK, ack, sizeof(ack), &ack_len, 5000U);
    if (rc != NET_OK) {
        return rc;
    }
    if (ack_len < 3U || ack[2] == 0x80U) {
        return NET_ERR_IO;
    }
    return NET_OK;
}

int mqtt_poll(mqtt_client_t *c, uint32_t timeout_ms)
{
    uint8_t body[MQTT_BUF_MAX];
    uint8_t type;
    uint32_t len = 0U;
    int rc;

    if (c == NULL || !c->connected) {
        return NET_ERR_PARAM;
    }
    rc = mqtt_rx(c, &type, body, sizeof(body), &len, timeout_ms);
    if (rc != NET_OK) {
        return rc;
    }
    return mqtt_dispatch(c, type, body, len);
}

int mqtt_ping(mqtt_client_t *c)
{
    uint8_t body[2];
    uint32_t len = 0U;
    int rc;

    if (c == NULL || !c->connected) {
        return NET_ERR_PARAM;
    }
    rc = mqtt_send(c, MQTT_PINGREQ, NULL, 0U);
    if (rc < 0) {
        return rc;
    }
    return mqtt_wait(c, MQTT_PINGRESP, body, sizeof(body), &len, 5000U);
}

int mqtt_disconnect(mqtt_client_t *c)
{
    if (c == NULL) {
        return NET_ERR_PARAM;
    }
    if (c->connected) {
        (void)mqtt_send(c, MQTT_DISCONNECT, NULL, 0U);
    }
    (void)net_stream_close(&c->stream);
    c->connected = false;
    return NET_OK;
}

bool mqtt_is_connected(const mqtt_client_t *c)
{
    return (c != NULL && c->connected);
}

#endif /* NET_ENABLE */
