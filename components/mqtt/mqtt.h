#ifndef MQTT_H
#define MQTT_H

/*
 * mqtt.h - 基于 net 的 MQTT 3.1.1 客户端（最小实现）
 *
 * 支持：CONNECT/CONNACK、PUBLISH（QoS 0/1 发送，QoS 0/1 接收）、
 *       SUBSCRIBE/SUBACK、PINGREQ/PINGRESP、DISCONNECT。
 * 不支持：QoS 2、遗嘱消息、TLS、分片重组。
 *
 * 使用方式：
 *   mqtt_client_init(&cli, on_message, ctx);
 *   mqtt_connect(&cli, host, port, &opt, 5000);
 *   mqtt_subscribe(&cli, "topic", 0);
 *   while (mqtt_poll(&cli, 1000) != NET_ERR_CLOSED) { ... }
 */

#include "net.h"

#ifdef __cplusplus
extern "C" {
#endif

#if NET_ENABLE

#define MQTT_TOPIC_MAX      96

typedef struct {
    const char *client_id;      /* 必填 */
    const char *username;       /* 可空 */
    const char *password;       /* 可空 */
    uint16_t    keepalive_s;    /* 0 表示默认 60 秒 */
    bool        clean_session;
} mqtt_options_t;

/* 收到 PUBLISH 时回调（topic 保证以 '\0' 结尾） */
typedef void (*mqtt_message_cb)(const char *topic, const uint8_t *payload,
                                uint32_t len, void *user);

typedef struct {
    net_sock_t    sock;
    net_stream_t  stream;
    bool          connected;
    uint16_t      next_pid;
    mqtt_message_cb cb;
    void         *user;
} mqtt_client_t;

int mqtt_client_init(mqtt_client_t *c, mqtt_message_cb cb, void *user);

/* 建立 TCP 连接并完成 CONNECT/CONNACK 握手，返回 NET_OK 表示 CONNACK 成功 */
int mqtt_connect(mqtt_client_t *c, const char *host, uint16_t port,
                 const mqtt_options_t *opt, uint32_t timeout_ms);

/* 发布消息；qos 支持 0/1（1 会等待 PUBACK） */
int mqtt_publish(mqtt_client_t *c, const char *topic, const void *payload,
                 uint32_t len, uint8_t qos, bool retain);

/* 订阅主题；等待 SUBACK 成功 */
int mqtt_subscribe(mqtt_client_t *c, const char *topic, uint8_t qos);

/* 处理一个入站包（超时返回 NET_ERR_TIMEOUT，对端关闭返回 NET_ERR_CLOSED） */
int mqtt_poll(mqtt_client_t *c, uint32_t timeout_ms);

/* 发送 PINGREQ 并等待 PINGRESP */
int mqtt_ping(mqtt_client_t *c);

/* 发送 DISCONNECT 并关闭连接 */
int mqtt_disconnect(mqtt_client_t *c);

bool mqtt_is_connected(const mqtt_client_t *c);

#endif /* NET_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* MQTT_H */
