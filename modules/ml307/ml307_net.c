/*
 * ml307_net.c - ML307 网络后端：实现 components/net 的原始接口
 *
 * 通过 MIP 系列 AT 指令提供 TCP/UDP 客户端连接、收发、DNS 解析与 ping；
 * 流式接口（net_stream_* / net_io_bind）直接复用 components/net 的通用实现。
 *
 * 本文件仅在 ML307 目标参与编译（构建系统排除 components/net/net_bsd.c）。
 */

#include "net.h"
#include "ml307.h"
#include "at.h"
#include "at_urc.h"
#include "hal_def.h"

#include <stdio.h>
#include <string.h>

#define ML307_NET_MAX_SOCKETS   6
#define ML307_NET_RX_MAX        512
#define ML307_NET_HOST_MAX      128
#define ML307_NET_DEFAULT_WAIT  30000U

typedef struct {
    bool used;
    bool connected;
    uint8_t rx[ML307_NET_RX_MAX];
    uint32_t rx_len;
} ml307_net_socket_t;

static ml307_net_socket_t s_sockets[ML307_NET_MAX_SOCKETS];
static bool s_urc_registered;

/* 解析十进制字段，p 向前推进 */
static int ml307_net_int(const char **p, const char *end)
{
    int v = 0;

    while (*p < end && (**p == ' ' || **p == '\t')) {
        (*p)++;
    }
    while (*p < end && **p >= '0' && **p <= '9') {
        v = v * 10 + (**p - '0');
        (*p)++;
    }
    return v;
}

/* +MIPURC: "rtcp"|"rudp",<id>,<len>,<data> 或 "disconn",<id>,<code> */
static void ml307_net_urc(const char *line, uint32_t len, void *user)
{
    const char *p;
    const char *end = line + len;
    char kind[16];
    uint32_t n = 0U;
    int id;

    (void)user;
    if (len < 9U || strncmp(line, "+MIPURC:", 8U) != 0) {
        return;
    }
    p = line + 8;
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }
    if (p >= end || *p != '"') {
        return;
    }
    p++;
    while (p < end && *p != '"' && n + 1U < sizeof(kind)) {
        kind[n++] = *p++;
    }
    kind[n] = '\0';
    if (p < end && *p == '"') {
        p++;
    }
    if (p < end && *p == ',') {
        p++;
    }

    id = ml307_net_int(&p, end);
    if (id < 0 || id >= ML307_NET_MAX_SOCKETS) {
        return;
    }
    if (p < end && *p == ',') {
        p++;
    }

    if (strcmp(kind, "disconn") == 0) {
        s_sockets[id].connected = false;
        return;
    }
    if (strcmp(kind, "rtcp") != 0 && strcmp(kind, "rudp") != 0) {
        return;
    }

    {
        int payload_len = ml307_net_int(&p, end);
        uint32_t avail;
        uint32_t copy;
        uint32_t room;

        if (payload_len <= 0) {
            return;
        }
        if (p < end && *p == ',') {
            p++;
        }
        avail = (uint32_t)(end - p);
        copy = ((uint32_t)payload_len < avail) ? (uint32_t)payload_len : avail;
        room = ML307_NET_RX_MAX - s_sockets[id].rx_len;
        if (copy > room) {
            copy = room;
        }
        memcpy(&s_sockets[id].rx[s_sockets[id].rx_len], p, copy);
        s_sockets[id].rx_len += copy;
    }
}

static bool ml307_net_host_valid(const char *host)
{
    size_t len;

    if (host == NULL || host[0] == '\0') {
        return false;
    }
    len = strlen(host);
    if (len >= ML307_NET_HOST_MAX) {
        return false;
    }
    return strchr(host, '"') == NULL && strchr(host, '\r') == NULL &&
           strchr(host, '\n') == NULL;
}

static int ml307_net_alloc(void)
{
    int i;

    for (i = 0; i < ML307_NET_MAX_SOCKETS; i++) {
        if (!s_sockets[i].used) {
            return i;
        }
    }
    return -1;
}

static bool ml307_net_ipv4(const char *s, size_t len)
{
    size_t i;
    int dots = 0;
    bool digit = false;

    for (i = 0; i < len; i++) {
        if (s[i] == '.') {
            dots++;
        } else if (s[i] >= '0' && s[i] <= '9') {
            digit = true;
        } else {
            return false;
        }
    }
    return dots == 3 && digit;
}

/* ---------------- net 原始接口 ---------------- */

int net_init(void)
{
    if (!s_urc_registered) {
        if (at_urc_register("+MIPURC:", ml307_net_urc, NULL) != 0) {
            return NET_ERR_NOMEM;
        }
        s_urc_registered = true;
    }
    memset(s_sockets, 0, sizeof(s_sockets));
    return NET_OK;
}

int net_deinit(void)
{
    int i;

    for (i = 0; i < ML307_NET_MAX_SOCKETS; i++) {
        if (s_sockets[i].used) {
            char cmd[32];

            snprintf(cmd, sizeof(cmd), "AT+MIPCLOSE=%d", i);
            (void)at_exec(cmd, 3000);
            s_sockets[i].used = false;
            s_sockets[i].connected = false;
            s_sockets[i].rx_len = 0U;
        }
    }
    if (s_urc_registered) {
        (void)at_urc_unregister("+MIPURC:");
        s_urc_registered = false;
    }
    return NET_OK;
}

int net_open(net_sock_t *sock, net_type_t type, const char *host,
             uint16_t port, uint32_t timeout_ms)
{
    char cmd[320];
    const char *proto;
    uint32_t wait = (timeout_ms != 0U) ? timeout_ms : ML307_NET_DEFAULT_WAIT;
    int id;

    if (sock == NULL || !ml307_net_host_valid(host)) {
        return NET_ERR_PARAM;
    }
    memset(sock, 0, sizeof(*sock));
    sock->fd = (intptr_t)(-1);
    proto = (type == NET_UDP) ? "UDP" : "TCP";

    id = ml307_net_alloc();
    if (id < 0) {
        return NET_ERR_NOMEM;
    }

    snprintf(cmd, sizeof(cmd), "AT+MIPOPEN=%d,\"%s\",\"%s\",%u",
             id, proto, host, (unsigned)port);
    if (at_exec(cmd, wait) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }

    s_sockets[id].used = true;
    s_sockets[id].connected = true;
    s_sockets[id].rx_len = 0U;
    sock->fd = (intptr_t)id;
    sock->type = (uint8_t)type;
    sock->used = true;
    return NET_OK;
}

bool net_is_open(const net_sock_t *sock)
{
    return (sock != NULL && sock->used && sock->fd >= 0 &&
            (int)sock->fd < ML307_NET_MAX_SOCKETS);
}

int net_send(net_sock_t *sock, const void *data, uint32_t len)
{
    char cmd[48];
    at_request_t phase;
    int id;

    if (!net_is_open(sock) || (data == NULL && len != 0U)) {
        return NET_ERR_PARAM;
    }
    id = (int)sock->fd;
    if (!s_sockets[id].connected) {
        return NET_ERR_CLOSED;
    }
    if (len == 0U) {
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "AT+MIPSEND=%d,%u", id, (unsigned)len);

    memset(&phase, 0, sizeof(phase));
    phase.command = cmd;
    phase.success = ">";
    phase.failure = "ERROR";
    phase.timeout = 5000U;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }

    at_send_raw((const uint8_t *)data, len);

    memset(&phase, 0, sizeof(phase));
    phase.command = NULL;
    phase.success = "OK";
    phase.failure = "ERROR";
    phase.timeout = 5000U;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    return (int)len;
}

int net_recv(net_sock_t *sock, void *data, uint32_t len, uint32_t timeout_ms)
{
    uint32_t start;
    int id;

    if (!net_is_open(sock) || data == NULL || len == 0U) {
        return NET_ERR_PARAM;
    }
    id = (int)sock->fd;

    start = hal_tick_get();
    for (;;) {
        if (s_sockets[id].rx_len > 0U) {
            uint32_t n = (s_sockets[id].rx_len < len) ? s_sockets[id].rx_len
                                                      : len;

            memcpy(data, s_sockets[id].rx, n);
            s_sockets[id].rx_len -= n;
            if (s_sockets[id].rx_len > 0U) {
                memmove(s_sockets[id].rx, &s_sockets[id].rx[n],
                        s_sockets[id].rx_len);
            }
            return (int)n;
        }
        if (!s_sockets[id].connected) {
            return NET_ERR_CLOSED;
        }
        if (timeout_ms == 0U ||
            (uint32_t)(hal_tick_get() - start) >= timeout_ms) {
            return 0;
        }
        (void)at_process();
        hal_delay_ms(1U);
    }
}

int net_close(net_sock_t *sock)
{
    char cmd[32];
    int id;

    if (sock == NULL) {
        return NET_ERR_PARAM;
    }
    if (net_is_open(sock)) {
        id = (int)sock->fd;
        snprintf(cmd, sizeof(cmd), "AT+MIPCLOSE=%d", id);
        (void)at_exec(cmd, 3000);
        s_sockets[id].used = false;
        s_sockets[id].connected = false;
        s_sockets[id].rx_len = 0U;
    }
    sock->fd = (intptr_t)(-1);
    sock->used = false;
    return NET_OK;
}

/* ---------------- DNS ---------------- */

int net_dns_resolve(const char *host, char *ip, uint32_t size)
{
    char cmd[192];
    const char *resp;
    const char *p;

    if (host == NULL || ip == NULL || size == 0U) {
        return NET_ERR_PARAM;
    }
    if (strlen(host) < size && ml307_net_ipv4(host, strlen(host))) {
        memcpy(ip, host, strlen(host) + 1U);
        return NET_OK;
    }

    snprintf(cmd, sizeof(cmd), "AT+MDNSGIP=\"%s\"", host);
    if (at_exec(cmd, 30000U) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }

    resp = at_get_response();
    p = (resp != NULL) ? strchr(resp, '"') : NULL;
    if (p == NULL) {
        return NET_ERR_IO;
    }
    while (*p != '\0') {
        const char *q;

        p = strchr(p, '"');
        if (p == NULL) {
            break;
        }
        p++;
        q = strchr(p, '"');
        if (q == NULL) {
            break;
        }
        if (ml307_net_ipv4(p, (size_t)(q - p))) {
            size_t n = (size_t)(q - p);

            if (n >= size) {
                n = size - 1U;
            }
            memcpy(ip, p, n);
            ip[n] = '\0';
            return NET_OK;
        }
        p = q + 1;
    }
    return NET_ERR_IO;
}
