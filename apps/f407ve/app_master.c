/*
 * app_master.c - F407VE 接入 IoT-DMP 平台
 *
 * 演示流程：
 *   1. app_ml307_ensure()：AT 引擎 / 模组握手 / PDP / net 后端；
 *   2. master_init()：HMAC-MD5 鉴权参数与 7 个下行主题订阅；
 *   3. 用 var_t 组装属性/状态参数，上报 status/report 与 property/report；
 *   4. 在指定时长内 master_tick() 轮询下行，收到指令后打印并回复。
 *
 * 下行回复策略（演示）：
 *   property/get  -> data 返回示例属性；
 *   service/call  -> data 返回 {"executed":true}；
 *   其它           -> ret=1，无 data。
 */

#include "app_master.h"

#include "app_ml307.h"
#include "master.h"
#include "net.h"
#include "ros.h"
#include "roshell.h"
#include "var_json.h"

#include <stdio.h>
#include <string.h>

#define DEMO_MASTER_HOST        "47.114.32.128"
#define DEMO_MASTER_PORT        1883U
#define DEMO_MASTER_DEVICE_ID   "abe3b1849d344789924e75adb249"
#define DEMO_MASTER_MODEL_ID    "36e65ed9b72b4610acaa45b2561a"
#define DEMO_MASTER_SECRET      "123456"
#define DEMO_MASTER_SECONDS     60U

/* --------------------------------------------------------- var 参数组装 */

/* 示例属性：{"temperature":25.5,"humidity":62.5,"light":0} */
static void make_property(var_t *out)
{
    if (var_object_new(out) == NULL) {
        return;
    }
    (void)var_object_put_float32(out, "temperature", 25.5f);
    (void)var_object_put_float32(out, "humidity", 62.5f);
    (void)var_object_put_int32(out, "light", 0);
}

/* 状态参数：{"firmware":"v1.0.0","version":"1.0.0","uptime":N} */
static void make_status(var_t *out, uint32_t uptime_s)
{
    if (var_object_new(out) == NULL) {
        return;
    }
    (void)var_object_put_str(out, "firmware", "v1.0.0");
    (void)var_object_put_str(out, "version", "1.0.0");
    (void)var_object_put_int32(out, "uptime", (int)uptime_s);
}

/* --------------------------------------------------------------- 下行回调 */

static void print_msg(const char *tag, const master_msg_t *msg)
{
    ros_printf("master <- [%s] %s\n", tag,
               (msg->topic != NULL) ? msg->topic : "?");
    if (msg->id != NULL && msg->id[0] != '\0') {
        ros_printf("  id      : %s\n", msg->id);
    }
    if (msg->service != NULL) {
        ros_printf("  service : %s\n", msg->service);
    }
    if (msg->params != NULL) {
        char *text = var_to_json(msg->params);

        if (text != NULL) {
            ros_printf("  params  : %s\n", text);
            var_free_json(text);
        }
    }
}

/* 只需回成功应答的主题 */
static void reply_simple(const char *tag, const master_msg_t *msg)
{
    print_msg(tag, msg);
    (void)master_reply(msg, 1, "success", NULL);
}

static void on_property_set(const master_msg_t *msg, void *user)
{
    (void)user;
    reply_simple("property/set", msg);
}

static void on_property_get(const master_msg_t *msg, void *user)
{
    var_t data;

    (void)user;
    print_msg("property/get", msg);
    make_property(&data);
    (void)master_reply(msg, 1, "success", &data);
    var_free(&data);
}

static void on_service_call(const master_msg_t *msg, void *user)
{
    var_t data;

    (void)user;
    print_msg("service/call", msg);
    if (var_object_new(&data) != NULL) {
        (void)var_object_put_bool(&data, "executed", true);
        (void)master_reply(msg, 1, "success", &data);
        var_free(&data);
    } else {
        (void)master_reply(msg, 1, "success", NULL);
    }
}

static void on_setting_set(const master_msg_t *msg, void *user)
{
    (void)user;
    reply_simple("setting/set", msg);
}

static void on_setting_get(const master_msg_t *msg, void *user)
{
    (void)user;
    reply_simple("setting/get", msg);
}

static void on_database_sync(const master_msg_t *msg, void *user)
{
    (void)user;
    reply_simple("database/sync", msg);
}

static void on_database_load(const master_msg_t *msg, void *user)
{
    (void)user;
    reply_simple("database/load", msg);
}

/* 平台对上报的应答（形如 xxx/response）：只打印，不回复 */
static void on_other(const master_msg_t *msg, void *user)
{
    (void)user;
    print_msg("other", msg);
}

static const master_callbacks_t s_master_cbs = {
    .property_set = on_property_set,
    .property_get = on_property_get,
    .service_call = on_service_call,
    .setting_set = on_setting_set,
    .setting_get = on_setting_get,
    .database_sync = on_database_sync,
    .database_load = on_database_load,
    .unknown = on_other
};

/* --------------------------------------------------------------- 命令实现 */

static int cmd_master(roshell_t *rs, int argc, char **argv,
                      const rs_args_parsed_t *parsed, void *user)
{
    master_config_t cfg;
    const char *host = NULL;
    uint32_t port = 0U;
    uint32_t seconds = 0U;
    uint32_t i;
    uint32_t loops;
    var_t props;
    var_t status;
    int rc;

    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;

    (void)rs_args_get_str(parsed, 0U, &host);
    (void)rs_args_get_uint(parsed, 1U, &port);
    (void)rs_args_get_uint(parsed, 2U, &seconds);

    if (app_ml307_ensure() != 0) {
        ros_puts("master: network init failed");
        return -1;
    }

    cfg.host = (host != NULL && host[0] != '\0') ? host : DEMO_MASTER_HOST;
    cfg.port = (port != 0U) ? (uint16_t)port : (uint16_t)DEMO_MASTER_PORT;
    cfg.device_id = DEMO_MASTER_DEVICE_ID;
    cfg.model_id = DEMO_MASTER_MODEL_ID;
    cfg.model_secret = DEMO_MASTER_SECRET;
    cfg.keepalive_s = 60U;
    cfg.clean_session = true;

    if (master_init(&cfg, &s_master_cbs) != 0) {
        ros_puts("master: init failed");
        return -1;
    }

    ros_printf("master: connect %s:%u id=%s\n", cfg.host, (unsigned)cfg.port,
               cfg.device_id);
    rc = master_start();
    if (rc != NET_OK) {
        ros_printf("master: connect/subscribe failed (%d)\n", rc);
        ros_puts("master: check model_id/model_secret (CONNACK=5 = auth fail)");
        return -1;
    }
    ros_puts("master: connected, 7 topics subscribed");

    make_status(&status, ros_tick_get() / 1000U);
    make_property(&props);
    ros_printf("master: status/report -> %d\n",
               master_report_status(&status));
    ros_printf("master: property/report -> %d\n",
               master_report_property(&props));
    var_free(&status);
    var_free(&props);

    loops = ((seconds != 0U) ? seconds : DEMO_MASTER_SECONDS) * 5U;
    ros_printf("master: polling %lu s\n", (unsigned long)(loops / 5U));
    for (i = 0U; i < loops; i++) {
        rc = master_tick(200U);
        if (rc == NET_ERR_CLOSED && (i % 5U) == 0U) {
            ros_puts("master: offline, retrying...");
        }
        ros_sleep_ms(50U);
    }

    master_stop();
    ros_puts("master: done");
    return 0;
}

/* --------------------------------------------------------- 大报文发布测试 */

#define BIG_PUB_BUF_SIZE    1200U

static char s_big_buf[BIG_PUB_BUF_SIZE];

/*
 * bigpub [size]
 *   构造一个 size 字节的 params.blob 字符串，发布到 property/report。
 *   超过 ML307 单条上限（1024）时由驱动直接返回错误。
 */
static int cmd_bigpub(roshell_t *rs, int argc, char **argv,
                      const rs_args_parsed_t *parsed, void *user)
{
    static const char head[] = "{\"id\":\"big-test\",\"params\":{\"blob\":\"";
    static const char tail[] = "\"},\"time\":0}";
    master_config_t cfg;
    uint32_t head_len = (uint32_t)(sizeof(head) - 1U);
    uint32_t tail_len = (uint32_t)(sizeof(tail) - 1U);
    uint32_t size = 0U;
    uint32_t total;
    uint32_t i;
    int rc;

    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;

    (void)rs_args_get_uint(parsed, 0U, &size);
    if (size == 0U) {
        size = 900U;
    }
    if (size > BIG_PUB_BUF_SIZE - head_len - tail_len) {
        size = BIG_PUB_BUF_SIZE - head_len - tail_len;
    }

    if (app_ml307_ensure() != 0) {
        ros_puts("bigpub: network init failed");
        return -1;
    }

    cfg.host = DEMO_MASTER_HOST;
    cfg.port = (uint16_t)DEMO_MASTER_PORT;
    cfg.device_id = DEMO_MASTER_DEVICE_ID;
    cfg.model_id = DEMO_MASTER_MODEL_ID;
    cfg.model_secret = DEMO_MASTER_SECRET;
    cfg.keepalive_s = 60U;
    cfg.clean_session = true;

    memcpy(s_big_buf, head, head_len);
    memset(&s_big_buf[head_len], 'A', size);
    memcpy(&s_big_buf[head_len + size], tail, tail_len);
    total = head_len + size + tail_len;
    s_big_buf[total] = '\0';

    if (master_init(&cfg, &s_master_cbs) != 0) {
        ros_puts("bigpub: init failed");
        return -1;
    }
    rc = master_start();
    if (rc != NET_OK) {
        ros_printf("bigpub: connect failed (%d)\n", rc);
        return -1;
    }

    ros_printf("bigpub: payload=%u bytes -> property/report\n",
               (unsigned)total);
    rc = master_publish_suffix("property/report", s_big_buf, total);
    ros_printf("bigpub: publish -> %d\n", rc);

    for (i = 0U; i < 25U; i++) {
        (void)master_tick(200U);
        ros_sleep_ms(50U);
    }
    master_stop();
    return (rc == 0) ? 0 : -1;
}

static const rs_args_t s_master_args[] = {
    { "host", 0, RS_ARGS_TYPE_STR, true, false, DEMO_MASTER_HOST, "broker host" },
    { "port", 0, RS_ARGS_TYPE_UINT, true, false, "1883", "broker port" },
    { "seconds", 0, RS_ARGS_TYPE_UINT, true, false, "60", "poll seconds" }
};

static const rs_cmd_t s_master_def = {
    .name = "master",
    .desc = "connect IoT-DMP platform and poll downlinks",
    .args = s_master_args,
    .arg_count = 3U,
    .on_start = cmd_master,
};

static const rs_args_t s_bigpub_args[] = {
    { "size", 0, RS_ARGS_TYPE_UINT, true, false, "900", "blob bytes" }
};

static const rs_cmd_t s_bigpub_def = {
    .name = "bigpub",
    .desc = "publish a large payload (200..1024) to platform",
    .args = s_bigpub_args,
    .arg_count = 1U,
    .on_start = cmd_bigpub,
};

void app_master_register(void)
{
    (void)rs_cmd_register(&s_master_def);
    (void)rs_cmd_register(&s_bigpub_def);
}
