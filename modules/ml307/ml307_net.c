/*
 * ml307_net.c - ML307R 网络后端：实现 components/net 的原始接口
 *
 * 通过 MIP 系列 AT 指令提供 TCP/UDP 客户端连接、收发与 DNS 解析
 * （ping 见 ml307_ping.c），流式接口复用 components/net 的通用实现。
 *
 * 指令序列（ML307R，TCP/IP 用户手册 V5.0.0）：
 *   AT+MIPOPEN=<id>,"TCP"|"UDP","<host>",<port>   -> OK，连接结果异步上报
 *   +MIPOPEN: <id>,<result>                        result=0 表示连接成功
 *   AT+MIPSEND=<id>,<len>                          -> '>' -> 原始数据 -> OK
 *   AT+MIPCLOSE=<id>
 *   +MIPURC: "rtcp"|"rudp",<id>,<len>,<data>       数据接收
 *   +MIPURC: "disconn",<id>,<state>                连接断开
 *   AT+MDNSGIP="<host>"                           -> +MDNSGIP: "<host>","<ip>"
 *
 * 本文件仅在 ML307 目标参与编译（构建系统排除 components/net/net_bsd.c）。
 */

#include "net.h"
#include "ml307.h"
#include "at.h"
#include "at_urc.h"
#include "hal_def.h"
#include "ros.h"
#include "common.h"

#include <stdio.h>

#ifndef ML307_NET_MAX_SOCKETS
#define ML307_NET_MAX_SOCKETS   6
#endif
#ifndef ML307_NET_RX_MAX
#define ML307_NET_RX_MAX        512
#endif
#define ML307_NET_HOST_MAX      128
#define ML307_NET_DEFAULT_WAIT  30000U

typedef struct {
    bool used;
    bool connected;
    uint8_t rx[ML307_NET_RX_MAX];
    uint32_t rx_len;
    uint32_t rx_dropped;   /* 接收缓冲满时被丢弃的字节数（可观测） */
} ml307_net_socket_t;

static ml307_net_socket_t s_sockets[ML307_NET_MAX_SOCKETS];
static bool s_urc_registered;

/* AT+MIPOPEN 的连接结果以 URC 异步上报，net_open 需等它 */
static volatile bool s_open_done;
static volatile int s_open_id;
static volatile int s_open_result;

/* AT+MDNSGIP 先回 OK，随后 +MDNSGIP: 才带解析结果，按 URC 收 */
static volatile bool s_dns_done;
static char s_dns_ip[48];

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

/* +MIPOPEN: <id>,<result>，result=0 表示连接成功，其余为失败 */
static void ml307_net_open_urc(const char *line, uint32_t len, void *user)
{
    const char *p;
    const char *end = line + len;
    int id;
    int result;

    (void)user;
    if (len < 10U || !str_starts_with(line, "+MIPOPEN:")) {
        return;
    }
    p = line + 9;
    id = ml307_net_int(&p, end);
    if (p < end && *p == ',') {
        p++;
    }
    result = ml307_net_int(&p, end);
    s_open_id = id;
    s_open_result = result;
    s_open_done = true;
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
    if (len < 9U || !str_starts_with(line, "+MIPURC:")) {
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
        /* rx_len/rx 与 net_read 消费者共享，需临界区保护 */
        ros_port_critical_enter();
        room = ML307_NET_RX_MAX - s_sockets[id].rx_len;
        if (copy > room) {
            s_sockets[id].rx_dropped += copy - room;   /* 缓冲满，记录丢弃 */
            copy = room;
        }
        memcpy(&s_sockets[id].rx[s_sockets[id].rx_len], p, copy);
        s_sockets[id].rx_len += copy;
        ros_port_critical_exit();
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

/* +MDNSGIP: "<domain>","<ip4>"[,"<ip6>"]，取第一个 IPv4 */
static void ml307_net_dns_urc(const char *line, uint32_t len, void *user)
{
    const char *p = line;
    const char *end = line + len;

    (void)user;
    if (len < 9U || !str_starts_with(line, "+MDNSGIP:")) {
        return;
    }
    p = line + 9;
    while (p < end) {
        const char *q;

        if (*p != '"') {
            p++;
            continue;
        }
        p++;
        q = p;
        while (q < end && *q != '"') {
            q++;
        }
        if (q >= end) {
            break;
        }
        if (ml307_net_ipv4(p, (size_t)(q - p)) &&
            (size_t)(q - p) < sizeof(s_dns_ip)) {
            memcpy(s_dns_ip, p, (size_t)(q - p));
            s_dns_ip[q - p] = '\0';
            s_dns_done = true;
            return;
        }
        p = q + 1;
    }
}

/* ---------------- net 原始接口 ---------------- */

int net_init(void)
{
    if (!s_urc_registered) {
        if (at_urc_register("+MIPURC:", ml307_net_urc, NULL) != 0 ||
            at_urc_register("+MIPOPEN:", ml307_net_open_urc, NULL) != 0 ||
            at_urc_register("+MDNSGIP:", ml307_net_dns_urc, NULL) != 0) {
            return NET_ERR_NOMEM;
        }
        s_urc_registered = true;
    }
    memset(s_sockets, 0, sizeof(s_sockets));
    net_register_commands();
    return NET_OK;
}

int net_deinit(void)
{
    int i;

    for (i = 0; i < ML307_NET_MAX_SOCKETS; i++) {
        if (s_sockets[i].used) {
            char cmd[32];

            str_printf(cmd, sizeof(cmd), "AT+MIPCLOSE=%d", i);
            (void)at_exec(cmd, 3000);
            s_sockets[i].used = false;
            s_sockets[i].connected = false;
            s_sockets[i].rx_len = 0U;
            s_sockets[i].rx_dropped = 0U;
        }
    }
    if (s_urc_registered) {
        (void)at_urc_unregister("+MIPURC:");
        (void)at_urc_unregister("+MIPOPEN:");
        (void)at_urc_unregister("+MDNSGIP:");
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
    uint32_t start;
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

    /* 先清结果，避免把上一条连接的 URC 当成这次的结果 */
    s_open_done = false;
    s_open_id = -1;
    s_open_result = -1;

    /* 模组可能残留同 id 的连接（例如上次异常退出），先关掉 */
    (void)str_printf(cmd, sizeof(cmd), "AT+MIPCLOSE=%d", id);
    (void)at_exec(cmd, 3000U);

    str_printf(cmd, sizeof(cmd), "AT+MIPOPEN=%d,\"%s\",\"%s\",%u",
             id, proto, host, (unsigned)port);
    if (at_exec(cmd, wait) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }

    /* OK 只表示命令被接受，真正连上要等 +MIPOPEN: <id>,0 */
    start = hal_tick_get();
    while (!(s_open_done && s_open_id == id) &&
           (uint32_t)(hal_tick_get() - start) < wait) {
        (void)at_process();
        hal_delay_ms(5U);
    }
    if (!s_open_done || s_open_id != id || s_open_result != 0) {
        /* 清掉模组侧半开连接 */
        (void)str_printf(cmd, sizeof(cmd), "AT+MIPCLOSE=%d", id);
        (void)at_exec(cmd, 3000U);
        return NET_ERR_IO;
    }

    s_sockets[id].used = true;
    s_sockets[id].connected = true;
    s_sockets[id].rx_len = 0U;
    s_sockets[id].rx_dropped = 0U;
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

    str_printf(cmd, sizeof(cmd), "AT+MIPSEND=%d,%u", id, (unsigned)len);

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
        ros_port_critical_enter();
        if (s_sockets[id].rx_len > 0U) {
            uint32_t n = (s_sockets[id].rx_len < len) ? s_sockets[id].rx_len
                                                      : len;

            memcpy(data, s_sockets[id].rx, n);
            s_sockets[id].rx_len -= n;
            if (s_sockets[id].rx_len > 0U) {
                memmove(s_sockets[id].rx, &s_sockets[id].rx[n],
                        s_sockets[id].rx_len);
            }
            ros_port_critical_exit();
            return (int)n;
        }
        ros_port_critical_exit();
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
        str_printf(cmd, sizeof(cmd), "AT+MIPCLOSE=%d", id);
        (void)at_exec(cmd, 3000);
        s_sockets[id].used = false;
        s_sockets[id].connected = false;
        s_sockets[id].rx_len = 0U;
        s_sockets[id].rx_dropped = 0U;
    }
    sock->fd = (intptr_t)(-1);
    sock->used = false;
    return NET_OK;
}

/* ---------------- DNS ---------------- */

int net_dns_resolve(const char *host, char *ip, uint32_t size)
{
    char cmd[192];
    uint32_t start;
    size_t len;

    if (host == NULL || ip == NULL || size == 0U) {
        return NET_ERR_PARAM;
    }
    if (strlen(host) < size && ml307_net_ipv4(host, strlen(host))) {
        memcpy(ip, host, strlen(host) + 1U);
        return NET_OK;
    }

    s_dns_done = false;
    s_dns_ip[0] = '\0';

    str_printf(cmd, sizeof(cmd), "AT+MDNSGIP=\"%s\"", host);
    if (at_exec(cmd, 30000U) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }

    /* OK 之后才上报 +MDNSGIP:，等 URC 带出 IPv4 */
    start = hal_tick_get();
    while (!s_dns_done && (uint32_t)(hal_tick_get() - start) < 30000U) {
        (void)at_process();
        hal_delay_ms(5U);
    }
    if (!s_dns_done) {
        return NET_ERR_IO;
    }
    len = strlen(s_dns_ip);
    if (len >= size) {
        len = size - 1U;
    }
    memcpy(ip, s_dns_ip, len);
    ip[len] = '\0';
    return NET_OK;
}
