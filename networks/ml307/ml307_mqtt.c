/*
 * ML307 MQTT 实现：使用 AT 指令驱动模组内置 MQTT。
 * 文件前半部分为 AT 原语，后半部分实现 ros_mqtt 接口。
 */
#include "ml307_mqtt.h"
#include "ros_mqtt.h"
#include "at.h"

#include <stdio.h>
#include <string.h>

int ml307_mqtt_connect(const char *host, uint16_t port,
                       const char *client_id, const char *user, const char *pass)
{
    char cmd[256];

    if (host == NULL || host[0] == '\0') {
        return -1;
    }
    (void)at_exec("AT+MQTTDISC=0", 3000);

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTCONN=0,\"%s\",%u,\"%s\",\"%s\",\"%s\"",
             host, (unsigned)port,
             client_id ? client_id : "",
             user ? user : "",
             pass ? pass : "");

    return (at_exec(cmd, 30000) == AT_RESULT_OK) ? 0 : -1;
}

int ml307_mqtt_disconnect(void)
{
    return (at_exec("AT+MQTTDISC=0", 3000) == AT_RESULT_OK)
           ? 0 : -1;
}

int ml307_mqtt_subscribe(const char *topic, uint8_t qos)
{
    char cmd[224];

    if (topic == NULL || topic[0] == '\0') {
        return -1;
    }
    snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",%u", topic, (unsigned)qos);
    return (at_exec(cmd, 5000) == AT_RESULT_OK) ? 0 : -1;
}

int ml307_mqtt_unsubscribe(const char *topic)
{
    char cmd[224];

    if (topic == NULL || topic[0] == '\0') {
        return -1;
    }
    snprintf(cmd, sizeof(cmd), "AT+MQTTUNSUB=0,\"%s\"", topic);
    return (at_exec(cmd, 5000) == AT_RESULT_OK) ? 0 : -1;
}

int ml307_mqtt_publish(const char *topic,
                       const uint8_t *payload, uint32_t len,
                       uint8_t qos, bool retain)
{
    char cmd[160];
    at_request_t phase;

    if (topic == NULL || payload == NULL || len == 0) {
        return -1;
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
    if (at_transact(&phase) != AT_RESULT_OK) {
        return -1;
    }

    /* 阶段2：发送载荷 */
    at_send_raw(payload, len);

    /* 阶段3：等待 OK */
    memset(&phase, 0, sizeof(phase));
    phase.command = NULL;
    phase.success = "OK";
    phase.failure = "ERROR";
    phase.timeout = 5000;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return -1;
    }
    return 0;
}

#if defined(ROS_NETWORK_ML307)

/* ---------------- ros_mqtt 接口实现 ---------------- */

static ros_mqtt_message_cb_t s_callback;
static void *s_user_data;
static bool s_connected;

int ros_mqtt_init(ros_mqtt_message_cb_t callback, void *user_data)
{
    s_callback = callback;
    s_user_data = user_data;
    return HAL_OK;
}

int ros_mqtt_deinit(void)
{
    if (s_connected) {
        (void)ml307_mqtt_disconnect();
        s_connected = false;
    }
    s_callback = NULL;
    s_user_data = NULL;
    return HAL_OK;
}

int ros_mqtt_connect(const ros_mqtt_config_t *config)
{
    if (config == NULL || config->host == NULL || config->host[0] == '\0') {
        return HAL_INVALID_PARAM;
    }
    if (ml307_mqtt_connect(config->host, config->port, config->client_id,
                           config->username, config->password) != 0) {
        s_connected = false;
        return HAL_IO_ERROR;
    }
    s_connected = true;
    return HAL_OK;
}

int ros_mqtt_disconnect(void)
{
    if (ml307_mqtt_disconnect() != 0) {
        return HAL_IO_ERROR;
    }
    s_connected = false;
    return HAL_OK;
}

int ros_mqtt_subscribe(const char *topic, uint8_t qos)
{
    if (topic == NULL || topic[0] == '\0') {
        return HAL_INVALID_PARAM;
    }
    return (ml307_mqtt_subscribe(topic, qos) == 0) ? HAL_OK : HAL_IO_ERROR;
}

int ros_mqtt_unsubscribe(const char *topic)
{
    if (topic == NULL || topic[0] == '\0') {
        return HAL_INVALID_PARAM;
    }
    return (ml307_mqtt_unsubscribe(topic) == 0) ? HAL_OK : HAL_IO_ERROR;
}

int ros_mqtt_publish(const ros_mqtt_message_t *message)
{
    if (message == NULL || message->topic[0] == '\0') {
        return HAL_INVALID_PARAM;
    }
    if (message->payload == NULL || message->payload_len == 0) {
        return HAL_INVALID_PARAM;
    }
    return (ml307_mqtt_publish(message->topic, message->payload,
                               message->payload_len, message->qos,
                               message->retain) == 0) ? HAL_OK : HAL_IO_ERROR;
}

int ros_mqtt_process(void)
{
    return HAL_OK;
}

bool ros_mqtt_is_connected(void)
{
    return s_connected;
}

#endif /* ROS_NETWORK_ML307 */
