/*
 * master.c - IoT-DMP 平台接入实现（MQTT 3.1.1）
 *
 * 组成：
 *   - 连接：HMAC-MD5 鉴权 + CONNECT/CONNACK，订阅 7 个下行主题；
 *   - 下行：var_from_json_ex() 解析完整 JSON，按主题分发到 master_callbacks_t；
 *   - 上行：var_t 组织报文，var_to_json() 序列化后发布；
 *   - 维护：master_tick() 中处理入站、心跳与断线重连。
 *
 * 说明：
 *   - 顶层主题为 "$thing/{device_id}"，所有报文均为紧凑 JSON；
 *   - 下行字符串存放在 dispatch 的栈上 arena 中，仅在回调期间有效；
 *   - Payload 缓冲固定，超长报文会被截断；
 *   - 单实例、非线程安全。
 */

#include "master.h"

#include "mqtt.h"
#include "ros.h"
#include "common.h"

#include <stdio.h>
#include <string.h>

/* 下行主题后缀（不含前缀） */
static const char *const k_down_suffix[] = {
    "property/set",
    "property/get",
    "service/call",
    "setting/set",
    "setting/get",
    "database/sync",
    "database/load"
};

typedef struct {
    bool     inited;
    bool     online;
    uint16_t port;
    uint16_t keepalive_s;
    bool     clean_session;

    char host[MASTER_HOST_MAX];
    char device_id[MASTER_ID_MAX];
    char model_id[MASTER_ID_MAX];
    char model_secret[MASTER_SECRET_MAX];
    char password[33];
    char prefix[MASTER_ID_MAX + 16U];   /* "$thing/{device_id}" */

    mqtt_client_t mqtt;
    master_callbacks_t cbs;

    uint32_t next_seq;
    uint32_t last_attempt;
    uint32_t last_ping;
} master_ctx_t;

static master_ctx_t s_m;

/* ============================================================================
 * 工具
 * ==========================================================================*/

static int copy_str(char *dst, size_t cap, const char *src)
{
    size_t len;

    if (src == NULL) {
        return -1;
    }
    len = strlen(src);
    if (len == 0U || len >= cap) {
        return -1;
    }
    memcpy(dst, src, len + 1U);
    return 0;
}

/* 生成 UUID 形态的消息 id：时间 + 序号，保证单实例内递增唯一 */
static void make_id(char *out, uint32_t cap)
{
    uint32_t t = ros_tick_get();
    uint32_t s = ++s_m.next_seq;

    (void)snprintf(out, cap, "%08lx-%04lx-4%03lx-8%03lx-%08lx%04lx",
                   (unsigned long)t, (unsigned long)(s & 0xFFFFU),
                   (unsigned long)((t >> 8) & 0x0FFFU),
                   (unsigned long)((s >> 4) & 0x0FFFU),
                   (unsigned long)(t ^ s), (unsigned long)(s & 0xFFFFU));
}

/* 当前 Unix 毫秒；RTC 未校时也按默认时间上报 */
static int64_t now_ms(void)
{
    return (int64_t)ros_rtc_get_timestamp() * 1000;
}

/* ============================================================================
 * 发布
 * ==========================================================================*/

static int publish_full_topic(const char *topic, const char *payload,
                              uint32_t len)
{
    if (!s_m.online || topic == NULL || payload == NULL || len == 0U) {
        return -1;
    }
    return mqtt_publish(&s_m.mqtt, topic, payload, len, 1U, false);
}

static int publish_full_topic_var(const char *topic, const var_t *root)
{
    char *text;
    int rc;

    if (topic == NULL || root == NULL) {
        return -1;
    }
    text = var_to_json(root);
    if (text == NULL) {
        return -1;
    }
    rc = publish_full_topic(topic, text, (uint32_t)strlen(text));
    var_free_json(text);
    return rc;
}

static int publish_suffix_var(const char *suffix, const var_t *root)
{
    char topic[96];

    if (suffix == NULL) {
        return -1;
    }
    if ((size_t)snprintf(topic, sizeof(topic), "%s/%s",
                         s_m.prefix, suffix) >= sizeof(topic)) {
        return -1;
    }
    return publish_full_topic_var(topic, root);
}

static int publish_payload(const char *suffix, const char *payload,
                           uint32_t len)
{
    char topic[96];

    if (suffix == NULL || payload == NULL) {
        return -1;
    }
    if ((size_t)snprintf(topic, sizeof(topic), "%s/%s",
                         s_m.prefix, suffix) >= sizeof(topic)) {
        return -1;
    }
    return publish_full_topic(topic, payload, len);
}

/* ============================================================================
 * 下行
 * ==========================================================================*/

static master_cmd_t suffix_to_cmd(const char *suffix)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t)(sizeof(k_down_suffix) / sizeof(k_down_suffix[0]));
         i++) {
        if (strcmp(suffix, k_down_suffix[i]) == 0) {
            return (master_cmd_t)i;
        }
    }
    return MASTER_CMD_UNKNOWN;
}

static master_topic_cb pick_callback(const master_callbacks_t *cbs,
                                     master_cmd_t cmd)
{
    if (cbs == NULL) {
        return NULL;
    }
    switch (cmd) {
    case MASTER_CMD_PROPERTY_SET:  return cbs->property_set;
    case MASTER_CMD_PROPERTY_GET:  return cbs->property_get;
    case MASTER_CMD_SERVICE_CALL:  return cbs->service_call;
    case MASTER_CMD_SETTING_SET:   return cbs->setting_set;
    case MASTER_CMD_SETTING_GET:   return cbs->setting_get;
    case MASTER_CMD_DATABASE_SYNC: return cbs->database_sync;
    case MASTER_CMD_DATABASE_LOAD: return cbs->database_load;
    default:                       return cbs->unknown;
    }
}

static void dispatch(const char *topic, const uint8_t *payload, uint32_t len)
{
    master_msg_t msg;
    master_topic_cb cb;
    char raw[MASTER_PAYLOAD_MAX];
    char arena[MASTER_PAYLOAD_MAX];
    var_t doc;
    const var_t *params;
    size_t plen;

    if (topic == NULL || payload == NULL) {
        return;
    }
    plen = strlen(s_m.prefix);
    if (strncmp(topic, s_m.prefix, plen) != 0 || topic[plen] != '/') {
        return;
    }
    if (len >= sizeof(raw)) {
        len = (uint32_t)sizeof(raw) - 1U;
    }
    memcpy(raw, payload, len);
    raw[len] = '\0';

    if (!var_from_json_ex(raw, &doc, arena, sizeof(arena))) {
        return;                         /* 非 JSON / 超长：丢弃 */
    }
    if (doc.type != VAR_OBJECT) {
        var_free(&doc);
        return;
    }

    params = var_object_get(&doc, "params");
    if (params == NULL) {
        params = var_object_get(&doc, "config");
    }
    if (params == NULL) {
        params = var_object_get(&doc, "keys");
    }

    memset(&msg, 0, sizeof(msg));
    msg.cmd = suffix_to_cmd(topic + plen + 1U);
    msg.topic = topic;
    msg.id = var_object_get_str(&doc, "id");
    if (msg.id == NULL) {
        msg.id = "";
    }
    msg.service = var_object_get_str(&doc, "service");
    msg.params = params;
    msg.raw_json = raw;
    msg.raw_len = len;

    cb = pick_callback(&s_m.cbs, msg.cmd);
    if (cb != NULL) {
        cb(&msg, s_m.cbs.user);
    } else if (msg.cmd != MASTER_CMD_UNKNOWN) {
        (void)master_reply(&msg, 1, "success", NULL);
    }

    var_free(&doc);
}

static void on_message(const char *topic, const uint8_t *payload, uint32_t len,
                       void *user)
{
    (void)user;
    dispatch(topic, payload, len);
}

/* ============================================================================
 * 连接
 * ==========================================================================*/

int master_init(const master_config_t *cfg, const master_callbacks_t *cbs)
{
    s_m.inited = false;
    s_m.online = false;
    if (cfg == NULL ||
        copy_str(s_m.host, sizeof(s_m.host), cfg->host) != 0 ||
        copy_str(s_m.device_id, sizeof(s_m.device_id), cfg->device_id) != 0 ||
        copy_str(s_m.model_id, sizeof(s_m.model_id), cfg->model_id) != 0 ||
        copy_str(s_m.model_secret, sizeof(s_m.model_secret),
                 cfg->model_secret) != 0) {
        return -1;
    }

    s_m.port = (cfg->port != 0U) ? cfg->port : 1883U;
    s_m.keepalive_s = (cfg->keepalive_s != 0U) ? cfg->keepalive_s : 60U;
    s_m.clean_session = cfg->clean_session;
    if (cbs != NULL) {
        s_m.cbs = *cbs;
    } else {
        memset(&s_m.cbs, 0, sizeof(s_m.cbs));
    }
    s_m.next_seq = 0U;
    s_m.last_attempt = 0U;
    s_m.last_ping = 0U;

    if (master_auth_password(s_m.model_id, s_m.device_id, s_m.model_secret,
                             s_m.password) != 0) {
        return -1;
    }
    if ((size_t)snprintf(s_m.prefix, sizeof(s_m.prefix), "$thing/%s",
                         s_m.device_id) >= sizeof(s_m.prefix)) {
        return -1;
    }

    (void)mqtt_client_init(&s_m.mqtt, on_message, NULL);
    s_m.inited = true;
    return 0;
}

int master_start(void)
{
    mqtt_options_t opt;
    uint32_t i;
    uint32_t failures = 0U;
    int rc;

    if (!s_m.inited) {
        return NET_ERR_PARAM;
    }
    if (s_m.online) {
        return 0;
    }

    (void)mqtt_client_init(&s_m.mqtt, on_message, NULL);
    memset(&opt, 0, sizeof(opt));
    opt.client_id = s_m.device_id;
    opt.username = s_m.model_id;
    opt.password = s_m.password;
    opt.keepalive_s = s_m.keepalive_s;
    opt.clean_session = s_m.clean_session;

    s_m.last_attempt = ros_tick_get();
    rc = mqtt_connect(&s_m.mqtt, s_m.host, s_m.port, &opt, 20000U);
    if (rc != NET_OK) {
        s_m.online = false;
        return rc;
    }

    for (i = 0U; i < (uint32_t)(sizeof(k_down_suffix) /
                                sizeof(k_down_suffix[0])); i++) {
        char topic[96];

        if ((size_t)snprintf(topic, sizeof(topic), "%s/%s",
                             s_m.prefix, k_down_suffix[i]) >= sizeof(topic)) {
            failures++;
            continue;
        }
        if (mqtt_subscribe(&s_m.mqtt, topic, 1U) != NET_OK) {
            failures++;
        }
    }

    s_m.online = true;
    s_m.last_ping = ros_tick_get();
    return (failures == 0U) ? 0 : NET_ERR_IO;
}

void master_stop(void)
{
    if (s_m.inited && s_m.online) {
        (void)mqtt_disconnect(&s_m.mqtt);
    }
    s_m.online = false;
}

bool master_is_online(void)
{
    return s_m.inited && s_m.online && mqtt_is_connected(&s_m.mqtt);
}

bool master_is_inited(void)
{
    return s_m.inited;
}

const char *master_host(void)
{
    return s_m.host;
}

uint16_t master_port(void)
{
    return s_m.port;
}

const char *master_device_id(void)
{
    return s_m.device_id;
}

const char *master_model_id(void)
{
    return s_m.model_id;
}

int master_tick(uint32_t timeout_ms)
{
    uint32_t now;
    uint32_t keepalive_ms;
    int rc;

    if (!s_m.inited) {
        return NET_ERR_PARAM;
    }

    if (!s_m.online) {
        now = ros_tick_get();
        if (s_m.last_attempt != 0U &&
            (uint32_t)(now - s_m.last_attempt) < 5000U) {
            return NET_ERR_CLOSED;      /* 重连退避中 */
        }
        rc = master_start();
        if (rc != NET_OK) {
            return rc;
        }
    }

    rc = mqtt_poll(&s_m.mqtt, timeout_ms);
    if (rc == NET_ERR_CLOSED || rc == NET_ERR_IO) {
        s_m.online = false;
        s_m.last_attempt = ros_tick_get();
        return rc;
    }

    keepalive_ms = (uint32_t)s_m.keepalive_s * 1000U / 2U;
    now = ros_tick_get();
    if (s_m.last_ping != 0U && (uint32_t)(now - s_m.last_ping) >= keepalive_ms) {
        s_m.last_ping = now;
        (void)mqtt_ping(&s_m.mqtt);
    }
    return rc;
}

/* ============================================================================
 * 上行
 * ==========================================================================*/

int master_reply(const master_msg_t *req, int ret, const char *message,
                 const var_t *data)
{
    var_t root;
    char topic[96];
    int rc;

    if (req == NULL || req->topic == NULL) {
        return -1;
    }
    if ((size_t)snprintf(topic, sizeof(topic), "%s/response",
                         req->topic) >= sizeof(topic)) {
        return -1;
    }

    if (var_object_new(&root) == NULL) {
        return -1;
    }
    (void)var_object_put_str(&root, "id", (req->id != NULL) ? req->id : "");
    (void)var_object_put_int32(&root, "ret", (ret != 0) ? 1 : 0);
    (void)var_object_put_str(&root, "message",
                             (message != NULL) ? message : "");
    if (data != NULL) {
        (void)var_object_put_var(&root, "data", data);
    }

    rc = publish_full_topic_var(topic, &root);
    var_free(&root);
    return rc;
}

int master_publish_suffix(const char *suffix, const char *payload, uint32_t len)
{
    return publish_payload(suffix, payload, len);
}

static int report_params(const char *suffix, const var_t *params)
{
    var_t root;
    char id[MASTER_ID_STR_MAX];
    int rc;

    if (!s_m.online) {
        return -1;
    }
    make_id(id, sizeof(id));

    if (var_object_new(&root) == NULL) {
        return -1;
    }
    (void)var_object_put_str(&root, "id", id);
    if (params != NULL) {
        (void)var_object_put_var(&root, "params", params);
    } else {
        var_t empty;

        if (var_object_new(&empty) != NULL) {
            (void)var_object_put_var(&root, "params", &empty);
            var_free(&empty);
        }
    }
    (void)var_object_put_int64(&root, "time", now_ms());

    rc = publish_suffix_var(suffix, &root);
    var_free(&root);
    return rc;
}

int master_report_property(const var_t *params)
{
    return report_params("property/report", params);
}

int master_report_status(const var_t *params)
{
    return report_params("status/report", params);
}

int master_report_event(const char *name, const char *type,
                        const var_t *params)
{
    var_t root;
    var_t event;
    char id[MASTER_ID_STR_MAX];
    int rc;

    if (!s_m.online || name == NULL || type == NULL) {
        return -1;
    }
    make_id(id, sizeof(id));

    if (var_object_new(&root) == NULL) {
        return -1;
    }
    if (var_object_new(&event) == NULL) {
        var_free(&root);
        return -1;
    }

    (void)var_object_put_str(&event, "name", name);
    (void)var_object_put_str(&event, "type", type);
    if (params != NULL) {
        (void)var_object_put_var(&event, "params", params);
    }
    (void)var_object_put_str(&root, "id", id);
    (void)var_object_put_var(&root, "event", &event);
    var_free(&event);

    rc = publish_suffix_var("event/report", &root);
    var_free(&root);
    return rc;
}

int master_report_ota(const char *task_id, const char *stage, int progress,
                      const char *error, const char *version)
{
    var_t root;
    char id[MASTER_ID_STR_MAX];
    int rc;

    if (!s_m.online || task_id == NULL || stage == NULL) {
        return -1;
    }
    make_id(id, sizeof(id));

    if (var_object_new(&root) == NULL) {
        return -1;
    }
    (void)var_object_put_str(&root, "id", id);
    (void)var_object_put_str(&root, "task_id", task_id);
    (void)var_object_put_str(&root, "stage", stage);
    (void)var_object_put_int32(&root, "progress", progress);
    (void)var_object_put_str(&root, "error", (error != NULL) ? error : "");
    (void)var_object_put_str(&root, "version",
                             (version != NULL) ? version : "");
    (void)var_object_put_int64(&root, "time", now_ms());

    rc = publish_suffix_var("ota/report", &root);
    var_free(&root);
    return rc;
}
