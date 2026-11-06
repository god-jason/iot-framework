#include "iot_ml307_mqtt.h"
#include "iot_at.h"

#include <stdio.h>
#include <string.h>

int iot_ml307_mqtt_connect(const char *host, uint16_t port,
                           const char *client_id, const char *user, const char *pass)
{
    char cmd[256];

    if (host == NULL || host[0] == '\0') {
        return IOT_ERR_PARAM;
    }
    (void)iot_at_exec("AT+MQTTDISC=0", 3000);

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTCONN=0,\"%s\",%u,\"%s\",\"%s\",\"%s\"",
             host, (unsigned)port,
             client_id ? client_id : "",
             user ? user : "",
             pass ? pass : "");

    return (iot_at_exec(cmd, 30000) == IOT_AT_RESULT_OK) ? IOT_OK : IOT_ERR_IO;
}

int iot_ml307_mqtt_disconnect(void)
{
    return (iot_at_exec("AT+MQTTDISC=0", 3000) == IOT_AT_RESULT_OK)
           ? IOT_OK : IOT_ERR_IO;
}

int iot_ml307_mqtt_subscribe(const char *topic, uint8_t qos)
{
    char cmd[224];

    if (topic == NULL || topic[0] == '\0') {
        return IOT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",%u", topic, (unsigned)qos);
    return (iot_at_exec(cmd, 5000) == IOT_AT_RESULT_OK) ? IOT_OK : IOT_ERR_IO;
}

int iot_ml307_mqtt_unsubscribe(const char *topic)
{
    char cmd[224];

    if (topic == NULL || topic[0] == '\0') {
        return IOT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+MQTTUNSUB=0,\"%s\"", topic);
    return (iot_at_exec(cmd, 5000) == IOT_AT_RESULT_OK) ? IOT_OK : IOT_ERR_IO;
}

int iot_ml307_mqtt_publish(const char *topic,
                           const uint8_t *payload, uint32_t len,
                           uint8_t qos, bool retain)
{
    char cmd[160];
    iot_at_request_t phase;

    if (topic == NULL || payload == NULL || len == 0) {
        return IOT_ERR_PARAM;
    }

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTPUB=0,\"%s\",%u,%u,0,%u",
             topic, (unsigned)qos, retain ? 1u : 0u, (unsigned)len);

    /* 阶段1：发送命令并等待 > 提示符 */
    memset(&phase, 0, sizeof(phase));
    phase.command = cmd;
    phase.success = ">";
    phase.failure = "ERROR";
    phase.timeout = 5000;
    if (iot_at_transact(&phase) != IOT_AT_RESULT_OK) {
        return IOT_ERR_IO;
    }

    /* 阶段2：发送载荷 */
    iot_at_send_raw(payload, len);

    /* 阶段3：等待 OK */
    memset(&phase, 0, sizeof(phase));
    phase.command = NULL;
    phase.success = "OK";
    phase.failure = "ERROR";
    phase.timeout = 5000;
    if (iot_at_transact(&phase) != IOT_AT_RESULT_OK) {
        return IOT_ERR_IO;
    }
    return IOT_OK;
}