/*
 * mqtt_test.c - mqtt 组件回归测试（本地最小 MQTT 服务端）
 */

#include "test.h"
#include "mqtt.h"
#include "net_test_util.h"

#include <stdio.h>
#include <string.h>

#if NET_ENABLE

#define MQTT_TEST_PORT  18083U

typedef struct {
    int count;
    char topics[4][64];
    char payloads[4][64];
    int pings;
} mqtt_srv_ctx_t;

static char s_topic[64];
static char s_payload[64];
static int s_msgs;

static void msg_cb(const char *topic, const uint8_t *payload, uint32_t len,
                   void *user)
{
    uint32_t n = len;

    (void)user;
    (void)snprintf(s_topic, sizeof(s_topic), "%s", topic);
    if (n >= sizeof(s_payload)) {
        n = (uint32_t)sizeof(s_payload) - 1U;
    }
    memcpy(s_payload, payload, n);
    s_payload[n] = '\0';
    s_msgs++;
}

static int srv_read_packet(intptr_t fd, uint8_t *type, uint8_t *body,
                           uint32_t cap, uint32_t *len)
{
    uint8_t b;
    uint32_t remaining = 0U;
    uint32_t mult = 1U;
    int i;

    if (ntu_recv_exact(fd, &b, 1U) != 1) {
        return -1;
    }
    *type = b;
    for (i = 0; i < 4; i++) {
        if (ntu_recv_exact(fd, &b, 1U) != 1) {
            return -1;
        }
        remaining += (uint32_t)(b & 0x7FU) * mult;
        mult *= 128U;
        if ((b & 0x80U) == 0U) {
            break;
        }
    }
    if (remaining > cap) {
        return -1;
    }
    if (remaining != 0U &&
        ntu_recv_exact(fd, body, remaining) != (int)remaining) {
        return -1;
    }
    *len = remaining;
    return 0;
}

static void srv_publish(intptr_t fd, const char *topic, const char *payload)
{
    uint8_t body[128];
    uint8_t hdr[2];
    uint32_t n = 0U;
    uint16_t tl = (uint16_t)strlen(topic);
    uint16_t pl = (uint16_t)strlen(payload);

    body[n++] = (uint8_t)(tl >> 8);
    body[n++] = (uint8_t)(tl & 0xFFU);
    memcpy(&body[n], topic, tl);
    n += tl;
    memcpy(&body[n], payload, pl);
    n += pl;

    hdr[0] = 0x30U;
    hdr[1] = (uint8_t)n;
    (void)ntu_send_all(fd, hdr, 2U);
    (void)ntu_send_all(fd, body, n);
}

static void mqtt_server(intptr_t fd, void *user)
{
    mqtt_srv_ctx_t *ctx = (mqtt_srv_ctx_t *)user;
    uint8_t type;
    uint8_t body[256];
    uint32_t len;

    for (;;) {
        uint8_t kind;

        if (srv_read_packet(fd, &type, body, sizeof(body), &len) != 0) {
            return;
        }
        kind = (uint8_t)(type & 0xF0U);
        if (kind == 0x10U) {                    /* CONNECT */
            static const uint8_t ack[4] = { 0x20, 0x02, 0x00, 0x00 };

            (void)ntu_send_all(fd, ack, sizeof(ack));
        } else if (kind == 0x80U) {             /* SUBSCRIBE */
            uint8_t ack[5];

            ack[0] = 0x90U;
            ack[1] = 0x03U;
            ack[2] = (len >= 2U) ? body[0] : 0U;
            ack[3] = (len >= 2U) ? body[1] : 0U;
            ack[4] = 0x00U;
            (void)ntu_send_all(fd, ack, sizeof(ack));
            srv_publish(fd, "srv/topic", "hello-in");
        } else if (kind == 0x30U) {             /* PUBLISH */
            uint16_t tl;
            uint32_t off;
            uint8_t qos = (uint8_t)((type >> 1) & 0x03U);
            uint16_t pid = 0U;

            if (len < 2U) {
                return;
            }
            tl = (uint16_t)(((uint16_t)body[0] << 8) | body[1]);
            off = 2U + (uint32_t)tl;
            if (off > len) {
                return;
            }
            if (qos > 0U) {
                pid = (uint16_t)(((uint16_t)body[off] << 8) | body[off + 1U]);
                off += 2U;
            }
            if (ctx->count < 4 && off <= len) {
                uint32_t tcopy = (tl < 63U) ? tl : 63U;
                uint32_t plen = len - off;

                memcpy(ctx->topics[ctx->count], &body[2], tcopy);
                ctx->topics[ctx->count][tcopy] = '\0';
                if (plen > 63U) {
                    plen = 63U;
                }
                memcpy(ctx->payloads[ctx->count], &body[off], plen);
                ctx->payloads[ctx->count][plen] = '\0';
                ctx->count++;
            }
            if (qos == 1U) {
                uint8_t ack[4];

                ack[0] = 0x40U;
                ack[1] = 0x02U;
                ack[2] = (uint8_t)(pid >> 8);
                ack[3] = (uint8_t)(pid & 0xFFU);
                (void)ntu_send_all(fd, ack, sizeof(ack));
            }
        } else if (kind == 0xC0U) {             /* PINGREQ */
            static const uint8_t pong[2] = { 0xD0, 0x00 };

            (void)ntu_send_all(fd, pong, sizeof(pong));
            ctx->pings++;
        } else if (kind == 0xE0U) {             /* DISCONNECT */
            return;
        }
    }
}

void ut_mqtt(void)
{
    mqtt_client_t cli;
    mqtt_options_t opt;
    mqtt_srv_ctx_t ctx;
    ntu_server_t srv;

    tf_begin("mqtt/连接 订阅 发布");

    TF_CHECK_INT(net_init(), NET_OK);
    memset(&ctx, 0, sizeof(ctx));
    TF_CHECK(ntu_server_start(&srv, MQTT_TEST_PORT, mqtt_server, &ctx));

    TF_CHECK_INT(mqtt_client_init(&cli, msg_cb, NULL), NET_OK);
    memset(&opt, 0, sizeof(opt));
    opt.client_id = "ut-client";
    opt.clean_session = true;
    opt.keepalive_s = 30U;

    TF_CHECK_INT(mqtt_connect(&cli, "127.0.0.1", MQTT_TEST_PORT, &opt, 3000U),
                 NET_OK);
    TF_CHECK(mqtt_is_connected(&cli));

    TF_CHECK_INT(mqtt_subscribe(&cli, "cli/topic", 0U), NET_OK);

    /* 服务端在 SUBACK 后下发的 PUBLISH */
    s_msgs = 0;
    TF_CHECK_INT(mqtt_poll(&cli, 3000U), NET_OK);
    TF_CHECK_INT(s_msgs, 1);
    TF_CHECK(strcmp(s_topic, "srv/topic") == 0);
    TF_CHECK(strcmp(s_payload, "hello-in") == 0);

    /* 空闲轮询超时 */
    TF_CHECK_INT(mqtt_poll(&cli, 150U), NET_ERR_TIMEOUT);

    /* QoS0 / QoS1 发布（QoS1 等待 PUBACK） */
    TF_CHECK_INT(mqtt_publish(&cli, "cli/topic", "hello-out", 9U, 0U, false),
                 NET_OK);
    TF_CHECK_INT(mqtt_publish(&cli, "cli/q1", "q1-data", 7U, 1U, false),
                 NET_OK);

    /* PINGREQ / PINGRESP */
    TF_CHECK_INT(mqtt_ping(&cli), NET_OK);

    TF_CHECK_INT(mqtt_disconnect(&cli), NET_OK);
    TF_CHECK(!mqtt_is_connected(&cli));

    ntu_server_stop(&srv);

    /* 服务端收到的两条发布 */
    TF_CHECK_INT(ctx.count, 2);
    TF_CHECK(strcmp(ctx.topics[0], "cli/topic") == 0);
    TF_CHECK(strcmp(ctx.payloads[0], "hello-out") == 0);
    TF_CHECK(strcmp(ctx.topics[1], "cli/q1") == 0);
    TF_CHECK(strcmp(ctx.payloads[1], "q1-data") == 0);
    TF_CHECK_INT(ctx.pings, 1);

    TF_CHECK_INT(net_deinit(), NET_OK);
    tf_end();
}

#else

void ut_mqtt(void)
{
    tf_begin("mqtt/连接 订阅 发布");
    tf_end();
}

#endif /* NET_ENABLE */
