/*
 * ML307 网络实现：使用 MIP 系列 AT 指令提供 TCP/UDP 连接、
 * 数据收发、DNS 解析、本机 IP 与网络时间。
 *
 * 注意：接收数据通过 +MIPURC URC 上报，AT 引擎按行解析，
 *       二进制载荷中若含换行符可能被截断。
 */
#include "ros_net.h"
#include "ml307.h"
#include "at.h"
#include "at_urc.h"

#include <stdio.h>
#include <string.h>

#if defined(ROS_NETWORK_ML307)

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
    uint32_t n = 0;
    int id;

    (void)user;

    if (len < 9 || strncmp(line, "+MIPURC:", 8) != 0) {
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
    while (p < end && *p != '"' && n + 1 < sizeof(kind)) {
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
        uint32_t room;
        uint32_t copy;

        if (p < end && *p == ',') {
            p++;
        }
        if (payload_len <= 0) {
            return;
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

/* ---------------- ros_net 接口实现 ---------------- */

int ros_net_init(void)
{
    if (!s_urc_registered) {
        if (at_urc_register("+MIPURC:", ml307_net_urc, NULL) != 0) {
            return HAL_ERROR;
        }
        s_urc_registered = true;
    }
    memset(s_sockets, 0, sizeof(s_sockets));
    return HAL_OK;
}

int ros_net_deinit(void)
{
    int i;

    for (i = 0; i < ML307_NET_MAX_SOCKETS; i++) {
        if (s_sockets[i].used) {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+MIPCLOSE=%d", i);
            (void)at_exec(cmd, 3000);
            s_sockets[i].used = false;
            s_sockets[i].connected = false;
            s_sockets[i].rx_len = 0;
        }
    }
    if (s_urc_registered) {
        (void)at_urc_unregister("+MIPURC:");
        s_urc_registered = false;
    }
    return HAL_OK;
}

ros_net_id_t ros_net_connect(ros_net_type_t type, const char *host,
                             uint16_t port, uint32_t timeout)
{
    char cmd[320];
    const char *proto;
    uint32_t wait = (timeout != 0) ? timeout : ML307_NET_DEFAULT_WAIT;
    int id;

    if (!ml307_net_host_valid(host)) {
        return ROS_NET_INVALID_ID;
    }
    proto = (type == ROS_NET_UDP) ? "UDP" : "TCP";

    id = ml307_net_alloc();
    if (id < 0) {
        return ROS_NET_INVALID_ID;
    }

    snprintf(cmd, sizeof(cmd), "AT+MIPOPEN=%d,\"%s\",\"%s\",%u",
             id, proto, host, (unsigned)port);
    if (at_exec(cmd, wait) != AT_RESULT_OK) {
        return ROS_NET_INVALID_ID;
    }

    s_sockets[id].used = true;
    s_sockets[id].connected = true;
    s_sockets[id].rx_len = 0;
    return id;
}

int32_t ros_net_send(ros_net_id_t id, const uint8_t *data, uint32_t len)
{
    char cmd[48];
    at_request_t phase;

    if (id < 0 || id >= ML307_NET_MAX_SOCKETS || !s_sockets[id].used ||
        !s_sockets[id].connected || data == NULL || len == 0) {
        return HAL_INVALID_PARAM;
    }

    snprintf(cmd, sizeof(cmd), "AT+MIPSEND=%d,%u", (int)id, (unsigned)len);

    memset(&phase, 0, sizeof(phase));
    phase.command = cmd;
    phase.success = ">";
    phase.failure = "ERROR";
    phase.timeout = 5000;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return HAL_IO_ERROR;
    }

    at_send_raw(data, len);

    memset(&phase, 0, sizeof(phase));
    phase.command = NULL;
    phase.success = "OK";
    phase.failure = "ERROR";
    phase.timeout = 5000;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return HAL_IO_ERROR;
    }

    return (int32_t)len;
}

int32_t ros_net_recv(ros_net_id_t id, uint8_t *data, uint32_t len,
                     uint32_t timeout)
{
    uint32_t start;

    if (id < 0 || id >= ML307_NET_MAX_SOCKETS || !s_sockets[id].used ||
        data == NULL || len == 0) {
        return HAL_INVALID_PARAM;
    }

    start = hal_tick_get();
    for (;;) {
        if (s_sockets[id].rx_len > 0) {
            uint32_t n = (s_sockets[id].rx_len < len) ? s_sockets[id].rx_len : len;

            memcpy(data, s_sockets[id].rx, n);
            s_sockets[id].rx_len -= n;
            if (s_sockets[id].rx_len > 0) {
                memmove(s_sockets[id].rx, &s_sockets[id].rx[n],
                        s_sockets[id].rx_len);
            }
            return (int32_t)n;
        }

        if (!s_sockets[id].connected) {
            return 0;
        }
        if (timeout == 0 ||
            (uint32_t)(hal_tick_get() - start) >= timeout) {
            return 0;
        }

        (void)at_process();
        hal_delay_ms(1);
    }
}

int ros_net_close(ros_net_id_t id)
{
    char cmd[32];

    if (id < 0 || id >= ML307_NET_MAX_SOCKETS || !s_sockets[id].used) {
        return HAL_NOT_FOUND;
    }

    snprintf(cmd, sizeof(cmd), "AT+MIPCLOSE=%d", (int)id);
    (void)at_exec(cmd, 3000);

    s_sockets[id].used = false;
    s_sockets[id].connected = false;
    s_sockets[id].rx_len = 0;
    return HAL_OK;
}

int ros_net_gethostbyname(const char *name, char *ip, uint32_t size)
{
    char cmd[192];
    const char *resp;
    const char *p;

    if (name == NULL || ip == NULL || size == 0) {
        return HAL_INVALID_PARAM;
    }

    if (strlen(name) < size && ml307_net_ipv4(name, strlen(name))) {
        memcpy(ip, name, strlen(name) + 1);
        return HAL_OK;
    }

    snprintf(cmd, sizeof(cmd), "AT+MDNSGIP=\"%s\"", name);
    if (at_exec(cmd, 30000) != AT_RESULT_OK) {
        return HAL_IO_ERROR;
    }

    resp = at_get_response();
    p = (resp != NULL) ? strchr(resp, '"') : NULL;
    if (p == NULL) {
        return HAL_IO_ERROR;
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
                n = size - 1;
            }
            memcpy(ip, p, n);
            ip[n] = '\0';
            return HAL_OK;
        }
        p = q + 1;
    }
    return HAL_NOT_FOUND;
}

int ros_net_getip(char *ip, uint32_t size)
{
    if (ip == NULL || size == 0) {
        return HAL_INVALID_PARAM;
    }
    return (ml307_get_ip(ip, size) == 0) ? HAL_OK : HAL_IO_ERROR;
}

/* 模组不提供 MAC 地址 */
int ros_net_getmac(char *mac, uint32_t size)
{
    (void)mac;
    (void)size;
    return HAL_NOT_SUPPORTED;
}

int ros_net_gettime(ros_net_time_t *time)
{
    const char *resp;
    const char *p;
    int yy, month, day, hour, minute, second, zone;

    if (time == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (at_exec("AT+CCLK?", 3000) != AT_RESULT_OK) {
        return HAL_IO_ERROR;
    }
    resp = at_get_response();
    p = (resp != NULL) ? strchr(resp, '"') : NULL;
    if (p == NULL) {
        return HAL_IO_ERROR;
    }
    if (sscanf(p, "\"%d/%d/%d,%d:%d:%d%d\"", &yy, &month, &day,
               &hour, &minute, &second, &zone) != 7) {
        return HAL_IO_ERROR;
    }

    time->year = 2000 + yy;
    time->month = month;
    time->day = day;
    time->hour = hour;
    time->minute = minute;
    time->second = second;
    time->zone_min = zone * 15;     /* 3GPP 时区单位为 15 分钟 */
    return HAL_OK;
}

/* ML307 的 MIP 指令只支持客户端连接，不提供本地监听与服务端 accept */
int ros_net_listen(ros_net_type_t type, const char *host,
                            uint16_t port, ros_net_id_t *id)
{
    (void)type;
    (void)host;
    (void)port;

    if (id != NULL) {
        *id = ROS_NET_INVALID_ID;
    }
    return HAL_NOT_SUPPORTED;
}

int ros_net_accept(ros_net_id_t listen_id, ros_net_id_t *conn_id,
                            char *peer_ip, uint32_t ip_size,
                            uint16_t *peer_port, uint32_t timeout)
{
    (void)listen_id;
    (void)peer_ip;
    (void)ip_size;
    (void)peer_port;
    (void)timeout;

    if (conn_id != NULL) {
        *conn_id = ROS_NET_INVALID_ID;
    }
    return HAL_NOT_SUPPORTED;
}

/* 模组 UDP 为连接式（MIPOPEN + MIPSEND），不支持无连接 sendto/recvfrom */
int32_t ros_net_sendto(ros_net_id_t id, const char *host, uint16_t port,
                       const uint8_t *data, uint32_t len)
{
    (void)id;
    (void)host;
    (void)port;
    (void)data;
    (void)len;
    return HAL_NOT_SUPPORTED;
}

int32_t ros_net_recvfrom(ros_net_id_t id, uint8_t *data, uint32_t len,
                         uint32_t timeout, char *peer_ip, uint32_t ip_size,
                         uint16_t *peer_port)
{
    (void)id;
    (void)data;
    (void)len;
    (void)timeout;
    (void)peer_ip;
    (void)ip_size;
    (void)peer_port;
    return HAL_NOT_SUPPORTED;
}

#endif /* ROS_NETWORK_ML307 */
