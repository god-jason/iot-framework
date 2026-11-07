#ifndef MQTT_H
#define MQTT_H

/*
 * MQTT 3.1.1 客户端（软件实现，与传输无关）。
 * 通过 mqtt_transport_t 抽象字节流（TCP / TLS / 串口透传均可）。
 * 保留 mqtt_ops_t 作为后端适配接口（可对接模组内置 MQTT）。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char host[128];
    uint16_t port;

    char client_id[64];
    char username[64];
    char password[64];

    uint16_t keepalive;
    bool clean_session;

    bool ssl;
} mqtt_config_t;

typedef struct {
    char topic[192];

    const uint8_t *payload;
    uint32_t payload_len;

    uint8_t qos;
    bool retain;
} mqtt_message_t;

typedef void (*mqtt_message_fn)(const mqtt_message_t *message, void *user);

/* 后端适配接口（init/connect/subscribe/...），供模组内置 MQTT 实现复用 */
typedef struct {
    int (*init)(void *ctx);
    int (*connect)(void *ctx, const mqtt_config_t *config);
    int (*disconnect)(void *ctx);
    int (*subscribe)(void *ctx, const char *topic, uint8_t qos);
    int (*unsubscribe)(void *ctx, const char *topic);
    int (*publish)(void *ctx, const mqtt_message_t *message);
    int (*process)(void *ctx);
    void *ctx;
} mqtt_ops_t;

/* ---- 软件 MQTT 客户端（基于字节流传输） ---- */
/* read 返回 >0 = 读取字节数，0 = 超时/无数据，<0 = 错误 */
typedef struct {
    int (*write)(void *ctx, const uint8_t *data, uint32_t len);
    int (*read)(void *ctx, uint8_t *data, uint32_t len, uint32_t timeout);
    uint32_t (*tick)(void *ctx);   /* 毫秒 tick，可为 NULL 则不做保活检测 */
    void *ctx;
} mqtt_transport_t;

typedef struct mqtt_client mqtt_client_t;

struct mqtt_client {
    mqtt_transport_t transport;
    mqtt_config_t config;
    mqtt_message_fn on_message;
    void *user;

    uint16_t next_packet_id;
    uint32_t last_io_ms;
    bool connected;

    uint8_t rx[1024];
    uint32_t rx_len;
};

/* 初始化客户端（transport/config 会拷贝到客户端内） */
int mqtt_client_init(mqtt_client_t *client, const mqtt_transport_t *transport,
                     const mqtt_config_t *config,
                     mqtt_message_fn on_message, void *user);

/* 连接：发送 CONNECT 并等待 CONNACK（阻塞） */
int mqtt_client_connect(mqtt_client_t *client);

int mqtt_client_disconnect(mqtt_client_t *client);

/* 订阅 / 取消订阅（阻塞等待 SUBACK/UNSUBACK） */
int mqtt_client_subscribe(mqtt_client_t *client, const char *topic, uint8_t qos);
int mqtt_client_unsubscribe(mqtt_client_t *client, const char *topic);

/* 发布消息。QoS0 不等待确认，QoS1 阻塞等待 PUBACK */
int mqtt_client_publish(mqtt_client_t *client, const mqtt_message_t *message);

/* 周期性处理：泵取数据、分发 PUBLISH、响应 PINGREQ、按 keepalive 发送 PINGREQ */
int mqtt_client_process(mqtt_client_t *client);

bool mqtt_client_is_connected(const mqtt_client_t *client);

/* 清理连接后的内部状态 */
void mqtt_client_reset(mqtt_client_t *client);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_H */