/*
 * board_net.c - STM32F103CBT6 网络测试（ML307 + MQTT）
 *
 * UART2(PA2/PA3) 连接 ML307 4G 模组，注册 roshell 命令：
 *
 *   mqtt [host] [port]    默认 broker.emqx.io:1883
 *
 * 命令依次执行：ML307 初始化 -> AT 握手 -> MQTT 连接 -> 订阅/发布
 * -> 轮询入站消息 -> 断开。模块未接或未联网时打印失败原因后返回。
 */

#include "board.h"
#include "ros.h"
#include "roshell.h"
#include "at.h"
#include "ml307.h"
#include "mqtt.h"

#include <string.h>

#define ML307_RX_BUF_SIZE       512U
#define ML307_LINE_BUF_SIZE     256U

#define MQTT_TEST_HOST          "broker.emqx.io"
#define MQTT_TEST_PORT          1883U
#define MQTT_TEST_TOPIC         "robotos/f103cb/status"
#define MQTT_TEST_CLIENT_ID     "robotos-f103cb"

static bool s_at_ready;
static uint8_t s_rx_buf[ML307_RX_BUF_SIZE];
static char s_line_buf[ML307_LINE_BUF_SIZE];
static mqtt_client_t s_mqtt;
static bool s_cmd_registered;

static void mqtt_on_message(const char *topic, const uint8_t *payload,
                            uint32_t len, void *user)
{
    (void)user;
    ros_printf("mqtt: %s = %.*s\n", topic, (int)len, (const char *)payload);
}

static int mqtt_cmd_handler(const rs_cmd_ctx_t *ctx)
{
    const char *host = NULL;
    uint32_t port = 0U;
    mqtt_options_t opt;
    int rc;

    (void)rs_args_get_str(ctx->parsed, 0U, &host);
    (void)rs_args_get_uint(ctx->parsed, 1U, &port);
    if (host == NULL || host[0] == '\0') {
        host = MQTT_TEST_HOST;
    }
    if (port == 0U) {
        port = MQTT_TEST_PORT;
    }

    ros_printf("ml307: test %s:%u\n", host, (unsigned)port);

    if (!s_at_ready) {
        at_config_t cfg;

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
        s_at_ready = true;
    }

    if (ml307_start() != 0) {
        ros_puts("ml307: no AT response (module not connected?)");
        return -1;
    }

    memset(&opt, 0, sizeof(opt));
    opt.client_id = MQTT_TEST_CLIENT_ID;
    opt.keepalive_s = 60U;
    opt.clean_session = true;

    (void)mqtt_client_init(&s_mqtt, mqtt_on_message, NULL);
    ros_puts("mqtt: connecting...");
    rc = mqtt_connect(&s_mqtt, host, (uint16_t)port, &opt, 8000U);
    if (rc != NET_OK) {
        ros_printf("mqtt: connect failed (%d)\n", rc);
        return -1;
    }
    ros_puts("mqtt: connected");

    (void)mqtt_subscribe(&s_mqtt, MQTT_TEST_TOPIC, 0U);
    if (mqtt_publish(&s_mqtt, MQTT_TEST_TOPIC, "hello", 5U, 0U, false) == NET_OK) {
        ros_puts("mqtt: publish ok");
    } else {
        ros_puts("mqtt: publish failed");
    }

    {
        int i;

        for (i = 0; i < 20; i++) {
            (void)mqtt_poll(&s_mqtt, 100U);
        }
    }
    (void)mqtt_disconnect(&s_mqtt);
    ros_puts("mqtt: done");
    return 0;
}

static const rs_args_t s_mqtt_args[] = {
    { "host", 0, RS_ARGS_TYPE_STR, true, false, MQTT_TEST_HOST, "broker host" },
    { "port", 0, RS_ARGS_TYPE_UINT, true, false, "1883", "broker port" }
};

static const rs_cmd_t s_mqtt_def = {
    "mqtt", "ML307 MQTT connect test [host] [port]", NULL, NULL,
    s_mqtt_args, 2U, mqtt_cmd_handler, NULL, NULL, NULL, NULL
};

void board_net_init(void)
{
    if (s_cmd_registered) {
        return;
    }
    if (rs_cmd_register(&s_mqtt_def) == 0) {
        s_cmd_registered = true;
    }
}
