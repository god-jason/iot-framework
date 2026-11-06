#ifndef IOT_ML307_MQTT_H
#define IOT_ML307_MQTT_H

/*
 * ML307 MQTT 原语（基于 iot_at 引擎，模组内置 MQTT）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int iot_ml307_mqtt_connect(const char *host, uint16_t port,
                           const char *client_id, const char *user, const char *pass);
int iot_ml307_mqtt_disconnect(void);

int iot_ml307_mqtt_subscribe(const char *topic, uint8_t qos);
int iot_ml307_mqtt_unsubscribe(const char *topic);

int iot_ml307_mqtt_publish(const char *topic,
                           const uint8_t *payload, uint32_t len,
                           uint8_t qos, bool retain);

#ifdef __cplusplus
}
#endif

#endif /* IOT_ML307_MQTT_H */