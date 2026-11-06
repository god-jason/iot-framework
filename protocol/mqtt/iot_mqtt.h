#ifndef IOT_MQTT_H
#define IOT_MQTT_H

/*
 * 通用 MQTT 数据结构与操作接口（与具体后端无关）。
 * 可对接软 MQTT Client 或模组内置 MQTT（mqtt_at）。
 */

#include "iot_types.h"

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
} iot_mqtt_config_t;

typedef struct {
    char topic[192];

    const uint8_t *payload;
    uint32_t payload_len;

    uint8_t qos;
    bool retain;
} iot_mqtt_message_t;

typedef void (*iot_mqtt_message_fn)(const iot_mqtt_message_t *message, void *user);

typedef struct {
    int (*init)(void *ctx);
    int (*connect)(void *ctx, const iot_mqtt_config_t *config);
    int (*disconnect)(void *ctx);
    int (*subscribe)(void *ctx, const char *topic, uint8_t qos);
    int (*unsubscribe)(void *ctx, const char *topic);
    int (*publish)(void *ctx, const iot_mqtt_message_t *message);
    int (*process)(void *ctx);
    void *ctx;
} iot_mqtt_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* IOT_MQTT_H */