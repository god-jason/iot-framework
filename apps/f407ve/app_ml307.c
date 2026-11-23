/*
 * app_ml307.c - STM32F407VET6 的 ML307(4G) 演示
 *
 * USART2(PA2/PA3) 连接 ML307R 模组。
 * 注意：F407 的 USART2 由 Cube HAL 轮询收字节（at 引擎已适配）；
 *       PA2/PA3 与 PWM 编号 2/3 冲突，接模组时这两路 PWM 不可用。
 *
 * 本文件只提供命令注册；真正的网络测试在 roshell 会话任务中执行，
 * 因为那里的任务栈更大，能容纳 HTTP/MQTT 的调用链。
 */

#include "app_ml307.h"

#include "board.h"
#include "ros.h"
#include "roshell.h"
#include "at.h"
#include "ml307.h"
#include "net.h"
#include "http.h"
#include "mqtt.h"
#include "common.h"

#include <string.h>

#define ML307_RX_BUF_SIZE       64U     /* 单次从 USART2 泵取的字节数 */
#define ML307_LINE_BUF_SIZE     224U    /* AT 命令行缓冲 */

#define DEMO_MQTT_HOST          "broker.emqx.io"
#define DEMO_MQTT_PORT          1883U
#define DEMO_MQTT_TOPIC         "robotos/f407ve/status"
#define DEMO_MQTT_CLIENT_ID     "robotos-f407ve"

#define DEMO_HTTP_URL           "http://www.example.com/"
#define DEMO_TCP_HOST           "www.baidu.com"
#define DEMO_TCP_PORT           80U
#define DEMO_PING_HOST          "www.baidu.com"

static bool s_at_ready;
static uint8_t s_rx_buf[ML307_RX_BUF_SIZE];
static char s_line_buf[ML307_LINE_BUF_SIZE];
static mqtt_client_t s_mqtt;

/* ------------------------------------------------------------------ 初始化 */

/* 首次使用时初始化 AT 引擎、模组握手与 net 后端（ping 命令也随之注册） */
static int demo_ensure(void)
{
    at_config_t cfg;

    if (s_at_ready) {
        return 0;
    }
    ros_puts("ml307: init uart2");
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = board_uart2();
    cfg.rx_buffer = s_rx_buf;
    cfg.rx_buffer_size = (uint32_t)sizeof(s_rx_buf);
    cfg.line_buffer = s_line_buf;
    cfg.line_buffer_size = (uint32_t)sizeof(s_line_buf);
    cfg.default_timeout = 500U;
    cfg.default_retry = 0U;
    cfg.echo_enabled = 0U;
    if (ml307_init(&cfg) != 0) {
        ros_puts("ml307: init failed");
        return -1;
    }
    if (ml307_start() != 0) {
        ros_puts("ml307: no AT response");
        return -1;
    }
    ros_puts("ml307: ready");
    /* 激活 PDP；模组可能已激活而返回错误，忽略继续 */
    (void)ml307_network_connect(NULL);
    if (net_init() != NET_OK) {
        ros_puts("net: init failed");
        return -1;
    }
    s_at_ready = true;
    return 0;
}

/* ------------------------------------------------------------- SIM 与信号 */

static int run_sim(void)
{
    char buf[64];
    int16_t csq = 0;
    int16_t ber = 0;

    ros_puts("--- SIM / module info ---");
    if (ml307_get_iccid(buf, sizeof(buf)) == 0) {
        ros_printf("iccid : %s\n", buf);
    } else {
        ros_puts("iccid : n/a");
    }
    if (ml307_get_imei(buf, sizeof(buf)) == 0) {
        ros_printf("imei  : %s\n", buf);
    } else {
        ros_puts("imei  : n/a");
    }
    if (ml307_get_imsi(buf, sizeof(buf)) == 0) {
        ros_printf("imsi  : %s\n", buf);
    } else {
        ros_puts("imsi  : n/a");
    }
    if (ml307_get_csq(&csq, &ber) == 0) {
        ros_printf("csq   : %d, ber=%d\n", (int)csq, (int)ber);
    } else {
        ros_puts("csq   : n/a");
    }
    ros_printf("reg   : %s\n", ml307_network_is_registered() ? "registered"
                                                             : "not registered");
    if (ml307_get_ip(buf, sizeof(buf)) == 0) {
        ros_printf("ip    : %s\n", buf);
    } else {
        ros_puts("ip    : n/a");
    }
    return 0;
}

/* ------------------------------------------------------------------- ping */

static void demo_ping_reply(uint32_t seq, uint32_t rtt_ms, bool ok, void *user)
{
    (void)user;
    if (ok) {
        ros_printf("ping: seq=%u time=%u ms\n", (unsigned)seq, (unsigned)rtt_ms);
    } else {
        ros_printf("ping: seq=%u timeout\n", (unsigned)seq);
    }
}

static int run_ping(const char *host, uint32_t count)
{
    net_ping_stat_t stat;
    int rc;

    ros_printf("--- PING %s ---\n", host);
    memset(&stat, 0, sizeof(stat));
    /* 模组 MPING 直接接受域名，内部自行解析，无需先做 DNS */
    rc = net_ping(host, count, 1000U, 1000U, demo_ping_reply, NULL, &stat);
    if (rc != NET_OK) {
        ros_printf("ping: failed (%d)\n", rc);
        return -1;
    }
    ros_printf("ping: sent=%u recv=%u min/avg/max=%u/%u/%u ms\n",
               (unsigned)stat.sent, (unsigned)stat.received,
               (unsigned)stat.min_ms, (unsigned)stat.avg_ms,
               (unsigned)stat.max_ms);
    return (stat.received != 0U) ? 0 : -1;
}

/* ------------------------------------------------------------------- http */

/* 响应体回调：只打印前 160 字节，够验证链路与状态码 */
static int http_show(const uint8_t *data, uint32_t len, void *user)
{
    uint32_t *left = (uint32_t *)user;
    uint32_t n = len;

    if (n > *left) {
        n = *left;
    }
    if (n > 0U) {
        ros_printf("%.*s", (int)n, (const char *)data);
    }
    *left -= n;
    return (*left == 0U) ? 1 : 0;    /* 非 0：中止接收 */
}

static int run_http(const char *url)
{
    http_response_t rsp;
    uint32_t left = 160U;
    int rc;

    ros_printf("--- HTTP GET %s ---\n", url);
    memset(&rsp, 0, sizeof(rsp));
    rc = http_get(url, NULL, 0U, &rsp, http_show, &left, 30000U);
    if (rc != NET_OK) {
        ros_printf("http: failed (%d)\n", rc);
        return -1;
    }
    ros_printf("\nhttp: status=%d len=%u type=%s\n", rsp.status,
               (unsigned)rsp.content_length,
               (rsp.content_type[0] != '\0') ? rsp.content_type : "-");
    return (rsp.status == 200) ? 0 : -1;
}

/* -------------------------------------------------------------- TCP (net) */

static int run_net(const char *host, uint16_t port)
{
    net_sock_t sock;
    uint8_t rx[96];
    char req[96];
    int n;
    uint32_t i;

    ros_printf("--- TCP %s:%u ---\n", host, (unsigned)port);
    if (net_open(&sock, NET_TCP, host, port, 15000U) != NET_OK) {
        ros_puts("net: open failed");
        return -1;
    }
    (void)str_printf(req, sizeof(req), "HEAD / HTTP/1.0\r\nHost: %s\r\n\r\n", host);
    if (net_send(&sock, req, (uint32_t)strlen(req)) <= 0) {
        ros_puts("net: send failed");
        (void)net_close(&sock);
        return -1;
    }
    n = net_recv(&sock, rx, sizeof(rx), 8000U);
    if (n <= 0) {
        ros_printf("net: recv %s (%d)\n", (n == 0) ? "timeout" : "error", n);
        (void)net_close(&sock);
        return -1;
    }
    for (i = 0U; i < (uint32_t)n; i++) {
        if (rx[i] == '\r' || rx[i] == '\n') {
            break;
        }
    }
    ros_printf("net: rx %d bytes, status=[%.*s]\n", n, (int)i, (const char *)rx);
    (void)net_close(&sock);
    return 0;
}

/* ------------------------------------------------------------------- mqtt */

static void mqtt_on_message(const char *topic, const uint8_t *payload,
                            uint32_t len, void *user)
{
    (void)user;
    ros_printf("mqtt: %s = %.*s\n", topic, (int)len, (const char *)payload);
}

static int run_mqtt(const char *host, uint16_t port)
{
    mqtt_options_t opt;
    int i;
    int rc;

    ros_printf("--- MQTT %s:%u ---\n", host, (unsigned)port);
    memset(&opt, 0, sizeof(opt));
    opt.client_id = DEMO_MQTT_CLIENT_ID;
    opt.keepalive_s = 60U;
    opt.clean_session = true;

    (void)mqtt_client_init(&s_mqtt, mqtt_on_message, NULL);
    ros_puts("mqtt: connecting...");
    rc = mqtt_connect(&s_mqtt, host, port, &opt, 20000U);
    if (rc != NET_OK) {
        ros_printf("mqtt: connect failed (%d)\n", rc);
        return -1;
    }
    ros_puts("mqtt: connected");
    if (mqtt_subscribe(&s_mqtt, DEMO_MQTT_TOPIC, 0U) == NET_OK) {
        ros_puts("mqtt: subscribe ok");
    } else {
        ros_puts("mqtt: subscribe failed");
    }
    if (mqtt_publish(&s_mqtt, DEMO_MQTT_TOPIC, "hello", 5U, 0U, false) == NET_OK) {
        ros_puts("mqtt: publish ok");
    } else {
        ros_puts("mqtt: publish failed");
    }
    for (i = 0; i < 50; i++) {
        (void)mqtt_poll(&s_mqtt, 100U);
    }
    (void)mqtt_disconnect(&s_mqtt);
    ros_puts("mqtt: done");
    return 0;
}

/* ------------------------------------------------------------------ 命令 */

static int cmd_all(const rs_cmd_ctx_t *ctx)
{
    (void)ctx;
    if (demo_ensure() != 0) {
        return -1;
    }
    (void)run_sim();
    (void)run_ping(DEMO_PING_HOST, 3U);
    (void)run_http(DEMO_HTTP_URL);
    (void)run_net(DEMO_TCP_HOST, (uint16_t)DEMO_TCP_PORT);
    (void)run_mqtt(DEMO_MQTT_HOST, (uint16_t)DEMO_MQTT_PORT);
    ros_puts("ml307: all tests done");
    return 0;
}

static int cmd_sim(const rs_cmd_ctx_t *ctx)
{
    (void)ctx;
    if (demo_ensure() != 0) {
        return -1;
    }
    return run_sim();
}

static int cmd_http(const rs_cmd_ctx_t *ctx)
{
    const char *url = NULL;

    if (demo_ensure() != 0) {
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 0U, &url);
    return run_http((url != NULL && url[0] != '\0') ? url : DEMO_HTTP_URL);
}

static int cmd_net(const rs_cmd_ctx_t *ctx)
{
    const char *host = NULL;
    uint32_t port = 0U;

    if (demo_ensure() != 0) {
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 0U, &host);
    (void)rs_args_get_uint(ctx->parsed, 1U, &port);
    if (host == NULL || host[0] == '\0') {
        host = DEMO_TCP_HOST;
    }
    if (port == 0U) {
        port = DEMO_TCP_PORT;
    }
    return run_net(host, (uint16_t)port);
}

static int cmd_mqtt(const rs_cmd_ctx_t *ctx)
{
    const char *host = NULL;
    uint32_t port = 0U;

    if (demo_ensure() != 0) {
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 0U, &host);
    (void)rs_args_get_uint(ctx->parsed, 1U, &port);
    if (host == NULL || host[0] == '\0') {
        host = DEMO_MQTT_HOST;
    }
    if (port == 0U) {
        port = DEMO_MQTT_PORT;
    }
    return run_mqtt(host, (uint16_t)port);
}

static const rs_args_t s_http_args[] = {
    { "url", 0, RS_ARGS_TYPE_STR, true, false, DEMO_HTTP_URL, "http url" }
};

static const rs_args_t s_host_port_args[] = {
    { "host", 0, RS_ARGS_TYPE_STR, true, false, DEMO_MQTT_HOST, "host" },
    { "port", 0, RS_ARGS_TYPE_UINT, true, false, "1883", "port" }
};

static const rs_args_t s_net_args[] = {
    { "host", 0, RS_ARGS_TYPE_STR, true, false, DEMO_TCP_HOST, "host" },
    { "port", 0, RS_ARGS_TYPE_UINT, true, false, "80", "port" }
};

static const rs_cmd_t s_all_def = {
    "ml307", "run all ML307 tests (sim/ping/http/tcp/mqtt)", NULL, NULL,
    NULL, 0U, cmd_all, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_sim_def = {
    "sim", "show ICCID/IMEI/IMSI/CSQ/reg/ip", NULL, NULL,
    NULL, 0U, cmd_sim, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_http_def = {
    "http", "HTTP GET test [url]", NULL, NULL,
    s_http_args, 1U, cmd_http, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_net_def = {
    "net", "TCP connect/send/recv test [host] [port]", NULL, NULL,
    s_net_args, 2U, cmd_net, NULL, NULL, NULL, NULL
};

static const rs_cmd_t s_mqtt_def = {
    "mqtt", "MQTT connect/sub/pub test [host] [port]", NULL, NULL,
    s_host_port_args, 2U, cmd_mqtt, NULL, NULL, NULL, NULL
};

/* 注册全部演示命令；重复调用由 roshell 处理 */
void app_ml307_register(void)
{
    (void)rs_cmd_register(&s_all_def);
    (void)rs_cmd_register(&s_sim_def);
    (void)rs_cmd_register(&s_http_def);
    (void)rs_cmd_register(&s_net_def);
    (void)rs_cmd_register(&s_mqtt_def);
}
