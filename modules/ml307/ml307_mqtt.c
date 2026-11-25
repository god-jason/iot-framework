/*
 * ml307_mqtt.c - ML307R 原生 MQTT 实现（AT+MQTT*），平替 components/mqtt
 *
 * 启用方式：ML307 目标的构建系统编译本文件，并排除 components/mqtt/mqtt.c。
 * 使用模组内置 MQTT 协议栈，避免在 MCU 上编解码报文。
 *
 * 指令序列（ML307R，connect_id 固定 0）：
 *   AT+MQTTDISC=0                                    清理旧连接（忽略结果）
 *   AT+MQTTCONN=0,"<host>",<port>,"<client_id>","<user>","<pass>"
 *   AT+MQTTSUB=0,"<topic>",<qos>
 *   AT+MQTTPUB=0,"<topic>",<qos>,<retain>,0,<len>   -> '>' -> 原始载荷
 *   AT+MQTTSTATE=0                                   查询连接状态
 *   AT+MQTTDISC=0
 * 入站消息 URC：
 *   +MQTTURC: "publish",<connect_id>,<qos>,"<topic>",<len>,<len>,<payload>
 *
 * 限制：载荷按文本传递（不允许 '"'、CR/LF、NUL），不支持 QoS2。
 *       模组 AT 引擎为全局单例，本文件的静态缓冲不可重入。
 */

#include "mqtt.h"
#include "ml307.h"
#include "at.h"
#include "at_urc.h"
#include "hal_def.h"
#include "ros.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>

#define ML307_MQTT_URC              "+MQTTURC:"
#define ML307_MQTT_CONN_ID          0U
#define ML307_MQTT_DEFAULT_MS       10000U
#define ML307_MQTT_CONNECT_MS       20000U     /* 域名解析 + 连接，留足时间 */
#define ML307_MQTT_PAYLOAD_MAX      192U

static bool s_urc;
static bool s_connected;
static volatile bool s_conn_urc;
static volatile int s_conn_err;
static volatile uint32_t s_rx_count;
static mqtt_message_cb s_cb;
static void *s_user;

/* AT 命令缓冲：静态分配，避免占用任务栈 */
static char s_cmd[320];

/* 从 p 起跳过一个逗号分隔的字段，返回下一个字段起点 */
static const char *ml307_mqtt_skip_field(const char *p)
{
    p = strchr(p, ',');
    return (p != NULL) ? p + 1 : NULL;
}

/*
 * 解析 +MQTTURC: "publish",<cid>,<qos>,"<topic>",<len>,<len>,<payload>
 * 只处理 publish 事件，其余 URC（conn/suback 等）忽略。
 * 说明：line 由 AT 解析器提供，已去 CR/LF 且以 '\0' 结尾，这里直接原地解析。
 */
static void ml307_mqtt_on_urc(const char *line, uint32_t len, void *user)
{
    char topic[MQTT_TOPIC_MAX];
    const char *p;
    const char *q;
    char *end;
    long plen;
    uint32_t n;
    uint32_t use;

    (void)user;
    if (line == NULL || len == 0U) {
        return;
    }
    if (strstr(line, "\"conn\"") != NULL) {
        const char *code = strrchr(line, ',');

        /* "conn",<cid>,<code>：code 非 0 表示连接失败 */
        if (code != NULL && strtol(code + 1, NULL, 10) == 0) {
            s_conn_urc = true;
        } else {
            s_conn_err = (code != NULL) ? (int)strtol(code + 1, NULL, 10) : -1;
        }
        return;
    }
    p = strstr(line, "\"publish\"");
    if (p == NULL) {
        return;
    }
    p += strlen("\"publish\"");

    /* 跳过 <cid>、<qos> 两个整数字段，随后是指引号包裹的主题 */
    p = ml307_mqtt_skip_field(p);
    if (p == NULL) {
        return;
    }
    p = ml307_mqtt_skip_field(p);
    if (p == NULL) {
        return;
    }
    p = ml307_mqtt_skip_field(p);
    if (p == NULL || *p != '"') {
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

    /* 两个长度字段（载荷长度、总长度）之后是载荷正文 */
    p = ml307_mqtt_skip_field(q + 1);
    if (p == NULL) {
        return;
    }
    plen = strtol(p, &end, 10);
    p = ml307_mqtt_skip_field(end);
    if (p == NULL) {
        return;
    }
    (void)strtol(p, &end, 10);
    p = ml307_mqtt_skip_field(end);
    if (p == NULL) {
        return;
    }

    use = (uint32_t)strcspn(p, "\r\n");
    if (plen > 0 && (uint32_t)plen < use) {
        use = (uint32_t)plen;
    }
    if (s_cb != NULL) {
        s_cb(topic, (const uint8_t *)p, use, s_user);
    }
    s_rx_count++;
}

static int ml307_mqtt_ensure_urc(void)
{
    if (!s_urc && at_urc_register(ML307_MQTT_URC, ml307_mqtt_on_urc, NULL) != 0) {
        return NET_ERR_NOMEM;
    }
    s_urc = true;
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
    const char *user;
    const char *pass;
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

    rc = ml307_mqtt_ensure_urc();
    if (rc != NET_OK) {
        return rc;
    }
    s_cb = c->cb;
    s_user = c->user;
    s_rx_count = 0U;

    /* 同一 connect_id 重复连接会返回错误：先断开旧连接 */
    (void)at_exec("AT+MQTTDISC=0", 2000U);

    if (str_printf(s_cmd, sizeof(s_cmd),
                   "AT+MQTTCONN=%u,\"%s\",%u,\"%s\",\"%s\",\"%s\"",
                   (unsigned)ML307_MQTT_CONN_ID, host, (unsigned)port,
                   opt->client_id, user, pass) <= 0) {
        return NET_ERR_PARAM;
    }
    if (at_exec(s_cmd, (wait < ML307_MQTT_CONNECT_MS)
                          ? ML307_MQTT_CONNECT_MS : wait) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }

    /* AT+MQTTCONN 只表示命令被接受，真正连上会补发 +MQTTURC:"conn" */
    s_conn_urc = false;
    s_conn_err = 0;
    {
        uint32_t t0 = hal_tick_get();

        while (!s_conn_urc && s_conn_err == 0 &&
               (uint32_t)(hal_tick_get() - t0) < ML307_MQTT_CONNECT_MS) {
            (void)at_process();
            ros_sleep_ms(20U);
        }
    }
    if (!s_conn_urc) {
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
    at_request_t req;

    if (c == NULL || !s_connected || topic == NULL ||
        (payload == NULL && len != 0U) || qos > 1U ||
        len > ML307_MQTT_PAYLOAD_MAX ||
        !ml307_mqtt_text_ok(topic, (uint32_t)strlen(topic))) {
        return NET_ERR_PARAM;
    }
    if (len != 0U && !ml307_mqtt_text_ok((const char *)payload, len)) {
        return NET_ERR_PARAM;   /* 文本式 AT 发布不接受引号/换行 */
    }
    if (str_printf(s_cmd, sizeof(s_cmd),
                   "AT+MQTTPUB=%u,\"%s\",%u,%u,0,%u",
                   (unsigned)ML307_MQTT_CONN_ID, topic,
                   (unsigned)qos, retain ? 1U : 0U, (unsigned)len) <= 0) {
        return NET_ERR_PARAM;
    }

    /* 先等模组返回 '>' 提示符，再发送原始载荷，最后等 OK */
    memset(&req, 0, sizeof(req));
    req.command = s_cmd;
    req.success = ">";
    req.failure = "ERROR";
    req.timeout = ML307_MQTT_DEFAULT_MS;
    if (at_transact(&req) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    if (len != 0U && at_send_raw((const uint8_t *)payload, len) != 0) {
        return NET_ERR_IO;
    }
    if (at_exec(NULL, ML307_MQTT_DEFAULT_MS) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    return NET_OK;
}

int mqtt_subscribe(mqtt_client_t *c, const char *topic, uint8_t qos)
{
    if (c == NULL || !s_connected || topic == NULL || qos > 1U ||
        !ml307_mqtt_text_ok(topic, (uint32_t)strlen(topic))) {
        return NET_ERR_PARAM;
    }
    if (str_printf(s_cmd, sizeof(s_cmd), "AT+MQTTSUB=%u,\"%s\",%u",
                   (unsigned)ML307_MQTT_CONN_ID, topic,
                   (unsigned)qos) <= 0) {
        return NET_ERR_PARAM;
    }
    if (at_exec(s_cmd, ML307_MQTT_DEFAULT_MS) != AT_RESULT_OK) {
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
        ros_sleep_ms(10U);
    }
}

int mqtt_ping(mqtt_client_t *c)
{
    if (c == NULL || !s_connected) {
        return NET_ERR_PARAM;
    }
    /* ML307R 无 MQTTPING 命令，用状态查询确认链路仍可用 */
    if (str_printf(s_cmd, sizeof(s_cmd), "AT+MQTTSTATE=%u",
                   (unsigned)ML307_MQTT_CONN_ID) <= 0) {
        return NET_ERR_PARAM;
    }
    if (at_exec(s_cmd, ML307_MQTT_DEFAULT_MS) != AT_RESULT_OK) {
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
        (void)at_exec("AT+MQTTDISC=0", 5000U);
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
