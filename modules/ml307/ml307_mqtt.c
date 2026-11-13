/*
 * ml307_mqtt.c - ML307 原生 MQTT 实现（AT+MQTT*），平替 components/mqtt
 *
 * 启用方式：ML307 目标的构建系统编译本文件，并排除 components/mqtt/mqtt.c。
 * 使用模组内置 MQTT 协议栈，避免在 MCU 上编解码报文。
 *
 * 指令序列（若模组固件版本不同，只需调整本文件）：
 *   AT+MQTTCFG="<host>",<port>,"<client_id>","<user>","<pass>",<keepalive>
 *   AT+MQTTCONN
 *   AT+MQTTSUB="<topic>",<qos>
 *   AT+MQTTPUB="<topic>",<qos>,<retain>,"<payload>"
 *   AT+MQTTPING
 *   AT+MQTTDISC
 * 入站消息 URC：
 *   +MQTTRECV: "<topic>",<qos>,<len>,"<payload>"
 *
 * 限制：载荷按文本传递（不允许 '"'、CR/LF、NUL），不支持 QoS2。
 */

#include "mqtt.h"
#include "ml307.h"
#include "at.h"
#include "at_urc.h"
#include "hal_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ML307_MQTT_URC              "+MQTTRECV:"
#define ML307_MQTT_DEFAULT_MS       10000U
#define ML307_MQTT_PAYLOAD_MAX      192U

static bool s_urc;
static bool s_connected;
static volatile uint32_t s_rx_count;
static mqtt_message_cb s_cb;
static void *s_user;

/* +MQTTRECV: "<topic>",<qos>,<len>,"<payload>" */
static void ml307_mqtt_on_urc(const char *line, uint32_t len, void *user)
{
    char buf[384];
    char topic[MQTT_TOPIC_MAX];
    char *p;
    char *q;
    uint32_t n = 0U;
    int qos;
    int plen;

    (void)user;
    if (line == NULL || len == 0U) {
        return;
    }
    if (len >= sizeof(buf)) {
        len = (uint32_t)sizeof(buf) - 1U;
    }
    memcpy(buf, line, len);
    buf[len] = '\0';
    {
        char *cr = strchr(buf, '\r');

        if (cr != NULL) {
            *cr = '\0';
        }
    }

    p = strchr(buf, ':');
    if (p == NULL) {
        return;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return;
    }
    p++;
    q = strchr(p, '"');
    if (q == NULL) {
        return;
    }
    n = (uint32_t)(q - p);
    if (n >= sizeof(topic)) {
        n = (uint32_t)sizeof(topic) - 1U;
    }
    memcpy(topic, p, n);
    topic[n] = '\0';

    p = q + 1;
    while (*p == ',' || *p == ' ') {
        p++;
    }
    qos = (int)strtol(p, &q, 10);
    (void)qos;
    p = q;
    while (*p == ',' || *p == ' ') {
        p++;
    }
    plen = (int)strtol(p, &q, 10);
    p = q;
    while (*p == ',' || *p == ' ') {
        p++;
    }

    if (*p == '"') {
        uint32_t avail;
        uint32_t use;

        p++;
        q = strrchr(p, '"');
        if (q == NULL) {
            return;
        }
        avail = (uint32_t)(q - p);
        use = (plen > 0 && (uint32_t)plen < avail) ? (uint32_t)plen : avail;
        if (s_cb != NULL) {
            s_cb(topic, (const uint8_t *)p, use, s_user);
        }
        s_rx_count++;
    }
}

static int ml307_mqtt_ensure_urc(void)
{
    if (!s_urc) {
        if (at_urc_register(ML307_MQTT_URC, ml307_mqtt_on_urc, NULL) != 0) {
            return NET_ERR_NOMEM;
        }
        s_urc = true;
    }
    return NET_OK;
}

/* 校验可安全放入 AT 引号内的文本 */
static bool ml307_mqtt_text_ok(const char *s, uint32_t len)
{
    uint32_t i;

    for (i = 0U; i < len; i++) {
        char c = s[i];

        if (c == '\0' || c == '"' || c == '\r' || c == '\n') {
            return false;
        }
    }
    return true;
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
    s_cb = cb;
    s_user = user;
    s_rx_count = 0U;
    return NET_OK;
}

int mqtt_connect(mqtt_client_t *c, const char *host, uint16_t port,
                 const mqtt_options_t *opt, uint32_t timeout_ms)
{
    char cmd[320];
    const char *user;
    const char *pass;
    uint16_t keepalive;
    uint32_t wait = (timeout_ms != 0U) ? timeout_ms : ML307_MQTT_DEFAULT_MS;
    int rc;

    if (c == NULL || host == NULL || opt == NULL ||
        opt->client_id == NULL || opt->client_id[0] == '\0') {
        return NET_ERR_PARAM;
    }
    if (!ml307_mqtt_text_ok(host, (uint32_t)strlen(host)) ||
        !ml307_mqtt_text_ok(opt->client_id,
                            (uint32_t)strlen(opt->client_id))) {
        return NET_ERR_PARAM;
    }
    user = (opt->username != NULL) ? opt->username : "";
    pass = (opt->password != NULL) ? opt->password : "";
    if (!ml307_mqtt_text_ok(user, (uint32_t)strlen(user)) ||
        !ml307_mqtt_text_ok(pass, (uint32_t)strlen(pass))) {
        return NET_ERR_PARAM;
    }
    keepalive = (opt->keepalive_s != 0U) ? opt->keepalive_s : 60U;

    rc = ml307_mqtt_ensure_urc();
    if (rc != NET_OK) {
        return rc;
    }
    s_cb = c->cb;
    s_user = c->user;
    s_rx_count = 0U;

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTCFG=\"%s\",%u,\"%s\",\"%s\",\"%s\",%u",
             host, (unsigned)port, opt->client_id, user, pass,
             (unsigned)keepalive);
    if (at_exec(cmd, wait) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    if (at_exec("AT+MQTTCONN", wait) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    s_connected = true;
    c->connected = true;
    c->next_pid = 1U;
    return NET_OK;
}

int mqtt_publish(mqtt_client_t *c, const char *topic, const void *payload,
                 uint32_t len, uint8_t qos, bool retain)
{
    char cmd[320];
    char text[ML307_MQTT_PAYLOAD_MAX];
    int n;

    if (c == NULL || !s_connected || topic == NULL ||
        (payload == NULL && len != 0U) || qos > 1U ||
        !ml307_mqtt_text_ok(topic, (uint32_t)strlen(topic))) {
        return NET_ERR_PARAM;
    }
    if (len >= sizeof(text)) {
        return NET_ERR_PARAM;
    }
    if (len != 0U) {
        if (!ml307_mqtt_text_ok((const char *)payload, len)) {
            return NET_ERR_PARAM;   /* 文本式 AT 发布不接受引号/换行 */
        }
        memcpy(text, payload, len);
    }
    text[len] = '\0';

    n = snprintf(cmd, sizeof(cmd), "AT+MQTTPUB=\"%s\",%u,%u,\"%s\"",
                 topic, (unsigned)qos, retain ? 1U : 0U, text);
    if (n <= 0 || n >= (int)sizeof(cmd)) {
        return NET_ERR_PARAM;
    }
    if (at_exec(cmd, ML307_MQTT_DEFAULT_MS) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    return NET_OK;
}

int mqtt_subscribe(mqtt_client_t *c, const char *topic, uint8_t qos)
{
    char cmd[160];

    if (c == NULL || !s_connected || topic == NULL || qos > 1U ||
        !ml307_mqtt_text_ok(topic, (uint32_t)strlen(topic))) {
        return NET_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=\"%s\",%u",
             topic, (unsigned)qos);
    if (at_exec(cmd, ML307_MQTT_DEFAULT_MS) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    return NET_OK;
}

int mqtt_poll(mqtt_client_t *c, uint32_t timeout_ms)
{
    uint32_t start;

    if (c == NULL || !s_connected) {
        return NET_ERR_CLOSED;
    }
    start = hal_tick_get();
    for (;;) {
        uint32_t before = s_rx_count;

        (void)at_process();
        if (s_rx_count != before) {
            return NET_OK;
        }
        if (timeout_ms == 0U ||
            (uint32_t)(hal_tick_get() - start) >= timeout_ms) {
            return NET_ERR_TIMEOUT;
        }
        hal_delay_ms(10U);
    }
}

int mqtt_ping(mqtt_client_t *c)
{
    if (c == NULL || !s_connected) {
        return NET_ERR_PARAM;
    }
    if (at_exec("AT+MQTTPING", ML307_MQTT_DEFAULT_MS) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    return NET_OK;
}

int mqtt_disconnect(mqtt_client_t *c)
{
    if (c == NULL) {
        return NET_ERR_PARAM;
    }
    if (s_connected) {
        (void)at_exec("AT+MQTTDISC", 5000U);
    }
    s_connected = false;
    c->connected = false;
    return NET_OK;
}

bool mqtt_is_connected(const mqtt_client_t *c)
{
    (void)c;
    return s_connected;
}
