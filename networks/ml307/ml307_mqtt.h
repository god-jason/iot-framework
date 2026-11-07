#ifndef ML307_MQTT_H
#define ML307_MQTT_H

/*
 * ML307 MQTT 原语（基于 at 引擎，模组内置 MQTT）。
 * 仅供 ml307_mqtt.c 内部与单元测试使用；
 * 对外接口见 iot_mqtt.h。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int ml307_mqtt_connect(const char *host, uint16_t port,
                           const char *client_id, const char *user, const char *pass);
int ml307_mqtt_disconnect(void);

int ml307_mqtt_subscribe(const char *topic, uint8_t qos);
int ml307_mqtt_unsubscribe(const char *topic);

int ml307_mqtt_publish(const char *topic,
                           const uint8_t *payload, uint32_t len,
                           uint8_t qos, bool retain);

#ifdef __cplusplus
}
#endif

#endif /* ML307_MQTT_H */
