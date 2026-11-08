/*
 * ETH 网络实现：基于 Mongoose 事件循环提供 TCP/UDP 客户端与服务端。
 *
 * 说明：
 *   - 所有连接都挂在共享的 mg_mgr 上，iot_net_recv/accept 内部驱动轮询；
 *   - 未连接的 UDP sendto 使用临时 Mongoose UDP 连接完成；
 *   - 本机 IP/MAC/时间通过系统接口获取。
 */
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__linux__)
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "eth_net.h"
#include "iot_net.h"

#if defined(IOT_NETWORK_ETH)

#define ETH_NET_MAX_CONN      8
#define ETH_NET_RX_MAX        2048
#define ETH_NET_ACCEPT_MAX    4
#define ETH_NET_URL_MAX       320
#define ETH_NET_DEFAULT_MS    10000U
#define ETH_NET_SENDTO_MS     3000U

typedef struct {
    bool used;
    bool listener;
    bool udp;
    bool connected;
    bool closed;
    bool has_peer;
    struct mg_connection *conn;
    struct mg_addr peer;
    uint8_t rx[ETH_NET_RX_MAX];
    uint32_t rx_len;
} eth_sock_t;

static struct mg_mgr s_mgr;
static bool s_ready;

static eth_sock_t s_socks[ETH_NET_MAX_CONN];
static int s_accept_q[ETH_NET_ACCEPT_MAX];
static uint32_t s_accept_n;

/* ---------------- Mongoose 管理器 ---------------- */

struct mg_mgr *eth_net_mgr(void)
{
    if (!s_ready) {
        mg_mgr_init(&s_mgr);
        s_ready = true;
    }
    return &s_mgr;
}

void eth_net_poll(uint32_t timeout_ms)
{
    mg_mgr_poll(eth_net_mgr(), (int)timeout_ms);
}

void eth_net_deinit(void)
{
    if (s_ready) {
        mg_mgr_free(&s_mgr);
        s_ready = false;
    }
}

/* ---------------- 连接槽 ---------------- */

static int eth_slot_alloc(void)
{
    int i;

    for (i = 0; i < ETH_NET_MAX_CONN; i++) {
        if (!s_socks[i].used) {
            memset(&s_socks[i], 0, sizeof(s_socks[i]));
            s_socks[i].used = true;
            return i;
        }
    }
    return -1;
}

static void eth_slot_free(int index)
{
    if (index < 0 || index >= ETH_NET_MAX_CONN) {
        return;
    }
    memset(&s_socks[index], 0, sizeof(s_socks[index]));
}

static void eth_accept_remove(int index)
{
    uint32_t i;

    for (i = 0; i < s_accept_n; i++) {
        if (s_accept_q[i] == index) {
            memmove(&s_accept_q[i], &s_accept_q[i + 1],
                    (s_accept_n - i - 1U) * sizeof(s_accept_q[0]));
            s_accept_n--;
            return;
        }
    }
}

static void eth_append(eth_sock_t *sock, const uint8_t *data, size_t len)
{
    uint32_t room = ETH_NET_RX_MAX - sock->rx_len;
    uint32_t n = ((uint32_t)len < room) ? (uint32_t)len : room;

    if (n > 0U) {
        memcpy(&sock->rx[sock->rx_len], data, n);
        sock->rx_len += n;
    }
}

static void eth_peer_text(const struct mg_addr *addr, char *ip, uint32_t ip_size,
                          uint16_t *port)
{
    if (ip != NULL && ip_size > 0) {
        mg_snprintf(ip, ip_size, "%M", mg_print_ip, addr);
    }
    if (port != NULL) {
        *port = ntohs(addr->port);
    }
}

/* ---------------- 事件回调 ---------------- */

static void eth_net_fn(struct mg_connection *c, int ev, void *ev_data)
{
    eth_sock_t *sock = (eth_sock_t *)c->fn_data;

    (void)ev_data;

    switch (ev) {
    case MG_EV_ACCEPT: {
        int index;

        if (sock == NULL || !sock->listener || sock->udp) {
            break;
        }
        index = eth_slot_alloc();
        if (index < 0) {
            c->is_closing = 1;
            break;
        }
        s_socks[index].listener = false;
        s_socks[index].udp = false;
        s_socks[index].connected = true;
        s_socks[index].conn = c;
        s_socks[index].peer = c->rem;
        s_socks[index].has_peer = true;
        c->fn_data = &s_socks[index];

        if (s_accept_n < ETH_NET_ACCEPT_MAX) {
            s_accept_q[s_accept_n++] = index;
        } else {
            c->is_closing = 1;
            eth_slot_free(index);
        }
        break;
    }

    case MG_EV_CONNECT:
        if (sock != NULL) {
            sock->connected = true;
            sock->closed = false;
            sock->peer = c->rem;
            sock->has_peer = true;
        }
        break;

    case MG_EV_READ:
        if (sock != NULL) {
            if (c->recv.len > 0U) {
                eth_append(sock, c->recv.buf, c->recv.len);
                c->recv.len = 0U;
            }
            if (sock->udp) {
                sock->peer = c->rem;
                sock->has_peer = true;
            }
        }
        break;

    case MG_EV_ERROR:
        if (sock != NULL) {
            sock->closed = true;
            sock->connected = false;
        }
        break;

    case MG_EV_CLOSE:
        if (sock != NULL) {
            sock->closed = true;
            sock->connected = false;
            if (sock->conn == c) {
                sock->conn = NULL;
            }
        }
        break;

    default:
        break;
    }
}

/* ---------------- iot_net 接口实现 ---------------- */

hal_status_t iot_net_init(void)
{
    (void)eth_net_mgr();
    memset(s_socks, 0, sizeof(s_socks));
    s_accept_n = 0U;
    return HAL_OK;
}

hal_status_t iot_net_deinit(void)
{
    int i;

    for (i = 0; i < ETH_NET_MAX_CONN; i++) {
        if (s_socks[i].used) {
            if (s_socks[i].conn != NULL) {
                s_socks[i].conn->is_closing = 1;
            }
            eth_slot_free(i);
        }
    }
    s_accept_n = 0U;
    eth_net_poll(0);
    return HAL_OK;
}

iot_net_id_t iot_net_connect(iot_net_type_t type, const char *host,
                             uint16_t port, uint32_t timeout)
{
    char url[ETH_NET_URL_MAX];
    uint32_t wait = (timeout != 0U) ? timeout : ETH_NET_DEFAULT_MS;
    uint32_t start;
    int index;

    if (host == NULL || host[0] == '\0') {
        return IOT_NET_INVALID_ID;
    }

    snprintf(url, sizeof(url), "%s://%s:%u",
             (type == IOT_NET_UDP) ? "udp" : "tcp", host, (unsigned)port);

    index = eth_slot_alloc();
    if (index < 0) {
        return IOT_NET_INVALID_ID;
    }
    s_socks[index].listener = false;
    s_socks[index].udp = (type == IOT_NET_UDP);
    s_socks[index].conn = mg_connect(eth_net_mgr(), url, eth_net_fn,
                                     &s_socks[index]);
    if (s_socks[index].conn == NULL) {
        eth_slot_free(index);
        return IOT_NET_INVALID_ID;
    }

    start = mg_millis();
    while (!s_socks[index].connected && !s_socks[index].closed) {
        if (mg_millis() - start >= wait) {
            break;
        }
        eth_net_poll(1);
    }

    if (!s_socks[index].connected) {
        if (s_socks[index].conn != NULL) {
            s_socks[index].conn->is_closing = 1;
            eth_net_poll(0);
        }
        eth_slot_free(index);
        return IOT_NET_INVALID_ID;
    }
    return index;
}

int32_t iot_net_send(iot_net_id_t id, const uint8_t *data, uint32_t len)
{
    if (id < 0 || id >= ETH_NET_MAX_CONN || !s_socks[id].used ||
        s_socks[id].listener || s_socks[id].conn == NULL ||
        data == NULL || len == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (!s_socks[id].connected) {
        return HAL_NOT_READY;
    }
    if (!mg_send(s_socks[id].conn, data, len)) {
        return HAL_IO_ERROR;
    }
    return (int32_t)len;
}

static int32_t eth_recv_common(int id, uint8_t *data, uint32_t len,
                               uint32_t timeout)
{
    uint32_t start = mg_millis();

    for (;;) {
        if (s_socks[id].rx_len > 0U) {
            uint32_t n = (s_socks[id].rx_len < len) ? s_socks[id].rx_len : len;

            memcpy(data, s_socks[id].rx, n);
            s_socks[id].rx_len -= n;
            if (s_socks[id].rx_len > 0U) {
                memmove(s_socks[id].rx, &s_socks[id].rx[n],
                        s_socks[id].rx_len);
            }
            return (int32_t)n;
        }

        if (s_socks[id].closed) {
            return 0;
        }
        if (timeout == 0U || (mg_millis() - start) >= timeout) {
            return 0;
        }
        eth_net_poll(1);
    }
}

int32_t iot_net_recv(iot_net_id_t id, uint8_t *data, uint32_t len,
                     uint32_t timeout)
{
    if (id < 0 || id >= ETH_NET_MAX_CONN || !s_socks[id].used ||
        s_socks[id].listener || data == NULL || len == 0U) {
        return HAL_INVALID_PARAM;
    }
    return eth_recv_common(id, data, len, timeout);
}

hal_status_t iot_net_close(iot_net_id_t id)
{
    if (id < 0 || id >= ETH_NET_MAX_CONN || !s_socks[id].used) {
        return HAL_NOT_FOUND;
    }

    eth_accept_remove(id);
    if (s_socks[id].conn != NULL) {
        s_socks[id].conn->is_closing = 1;
        eth_net_poll(0);
    }
    eth_slot_free(id);
    return HAL_OK;
}

hal_status_t iot_net_listen(iot_net_type_t type, const char *host,
                            uint16_t port, iot_net_id_t *id)
{
    char url[ETH_NET_URL_MAX];
    const char *node;
    int index;

    if (id == NULL || (type != IOT_NET_TCP && type != IOT_NET_UDP)) {
        return HAL_INVALID_PARAM;
    }
    *id = IOT_NET_INVALID_ID;

    node = (host != NULL && host[0] != '\0') ? host : "0.0.0.0";
    snprintf(url, sizeof(url), "%s://%s:%u",
             (type == IOT_NET_UDP) ? "udp" : "tcp", node, (unsigned)port);

    index = eth_slot_alloc();
    if (index < 0) {
        return HAL_NO_MEMORY;
    }
    s_socks[index].listener = true;
    s_socks[index].udp = (type == IOT_NET_UDP);
    s_socks[index].conn = mg_listen(eth_net_mgr(), url, eth_net_fn,
                                    &s_socks[index]);
    if (s_socks[index].conn == NULL) {
        eth_slot_free(index);
        return HAL_IO_ERROR;
    }
    s_socks[index].connected = true;
    *id = index;
    return HAL_OK;
}

hal_status_t iot_net_accept(iot_net_id_t listen_id, iot_net_id_t *conn_id,
                            char *peer_ip, uint32_t ip_size,
                            uint16_t *peer_port, uint32_t timeout)
{
    uint32_t start;
    int index;

    if (listen_id < 0 || listen_id >= ETH_NET_MAX_CONN || conn_id == NULL ||
        !s_socks[listen_id].used || !s_socks[listen_id].listener ||
        s_socks[listen_id].udp || (peer_ip != NULL && ip_size == 0U)) {
        return HAL_INVALID_PARAM;
    }
    *conn_id = IOT_NET_INVALID_ID;

    start = mg_millis();
    while (s_accept_n == 0U) {
        if (timeout == 0U || (mg_millis() - start) >= timeout) {
            return HAL_TIMEOUT;
        }
        eth_net_poll(1);
    }

    index = s_accept_q[0];
    memmove(&s_accept_q[0], &s_accept_q[1],
            (s_accept_n - 1U) * sizeof(s_accept_q[0]));
    s_accept_n--;

    *conn_id = index;
    if (s_socks[index].has_peer) {
        eth_peer_text(&s_socks[index].peer, peer_ip, ip_size, peer_port);
    }
    return HAL_OK;
}

/* 未连接 UDP 发送的临时上下文 */
typedef struct {
    bool connected;
    bool closed;
} eth_sendto_ctx_t;

static eth_sendto_ctx_t s_tx;

static void eth_sendto_fn(struct mg_connection *c, int ev, void *ev_data)
{
    (void)c;
    (void)ev_data;

    if (ev == MG_EV_CONNECT) {
        s_tx.connected = true;
    } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
        s_tx.closed = true;
    }
}

int32_t iot_net_sendto(iot_net_id_t id, const char *host, uint16_t port,
                       const uint8_t *data, uint32_t len)
{
    char url[ETH_NET_URL_MAX];
    struct mg_connection *c;
    uint32_t start;

    if (id < 0 || id >= ETH_NET_MAX_CONN || !s_socks[id].used ||
        !s_socks[id].udp || host == NULL || host[0] == '\0' ||
        data == NULL || len == 0U) {
        return HAL_INVALID_PARAM;
    }

    /* 已连接的 UDP 客户端直接发送 */
    if (!s_socks[id].listener && s_socks[id].conn != NULL &&
        s_socks[id].connected) {
        if (!mg_send(s_socks[id].conn, data, len)) {
            return HAL_IO_ERROR;
        }
        return (int32_t)len;
    }

    snprintf(url, sizeof(url), "udp://%s:%u", host, (unsigned)port);
    s_tx.connected = false;
    s_tx.closed = false;
    c = mg_connect(eth_net_mgr(), url, eth_sendto_fn, NULL);
    if (c == NULL) {
        return HAL_IO_ERROR;
    }

    start = mg_millis();
    while (!s_tx.connected && !s_tx.closed) {
        if (mg_millis() - start >= ETH_NET_SENDTO_MS) {
            break;
        }
        eth_net_poll(1);
    }

    if (!s_tx.connected || !mg_send(c, data, len)) {
        c->is_closing = 1;
        eth_net_poll(0);
        return HAL_IO_ERROR;
    }

    c->is_closing = 1;
    eth_net_poll(0);
    return (int32_t)len;
}

int32_t iot_net_recvfrom(iot_net_id_t id, uint8_t *data, uint32_t len,
                         uint32_t timeout, char *peer_ip, uint32_t ip_size,
                         uint16_t *peer_port)
{
    int32_t n;

    if (id < 0 || id >= ETH_NET_MAX_CONN || !s_socks[id].used ||
        !s_socks[id].udp || data == NULL || len == 0U ||
        (peer_ip != NULL && ip_size == 0U)) {
        return HAL_INVALID_PARAM;
    }

    n = eth_recv_common(id, data, len, timeout);
    if (n > 0 && s_socks[id].has_peer) {
        eth_peer_text(&s_socks[id].peer, peer_ip, ip_size, peer_port);
    }
    return n;
}

/* ---------------- 系统信息 ---------------- */

hal_status_t iot_net_gethostbyname(const char *name, char *ip, uint32_t size)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    hal_status_t status = HAL_NOT_FOUND;

    if (name == NULL || ip == NULL || size == 0U) {
        return HAL_INVALID_PARAM;
    }

    (void)eth_net_mgr();

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(name, NULL, &hints, &res) != 0) {
        return HAL_IO_ERROR;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *sin = (struct sockaddr_in *)p->ai_addr;
        const char *text = inet_ntoa(sin->sin_addr);

        if (text != NULL) {
            snprintf(ip, size, "%s", text);
            status = HAL_OK;
            break;
        }
    }

    freeaddrinfo(res);
    return status;
}

hal_status_t iot_net_getip(char *ip, uint32_t size)
{
    char host[256];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;

    if (ip == NULL || size == 0U) {
        return HAL_INVALID_PARAM;
    }

    (void)eth_net_mgr();

    if (gethostname(host, (int)sizeof(host)) != 0) {
        return HAL_IO_ERROR;
    }
    host[sizeof(host) - 1] = '\0';

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        return HAL_IO_ERROR;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *sin = (struct sockaddr_in *)p->ai_addr;
        const char *text = inet_ntoa(sin->sin_addr);

        if (text != NULL) {
            snprintf(ip, size, "%s", text);
            freeaddrinfo(res);
            return HAL_OK;
        }
    }

    freeaddrinfo(res);
    return HAL_NOT_FOUND;
}

static hal_status_t eth_mac_text(const uint8_t *addr, char *mac, uint32_t size)
{
    if (size < IOT_NET_MAC_MAX) {
        return HAL_OVERFLOW;
    }
    snprintf(mac, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    return HAL_OK;
}

hal_status_t iot_net_getmac(char *mac, uint32_t size)
{
    if (mac == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (size < IOT_NET_MAC_MAX) {
        return HAL_OVERFLOW;
    }

#if defined(_WIN32)
    {
        IP_ADAPTER_ADDRESSES *addrs = NULL;
        ULONG len = 15000;
        ULONG ret;
        hal_status_t status = HAL_NOT_FOUND;
        int attempt;

        for (attempt = 0; attempt < 3; attempt++) {
            addrs = (IP_ADAPTER_ADDRESSES *)malloc(len);
            if (addrs == NULL) {
                return HAL_NO_MEMORY;
            }
            ret = GetAdaptersAddresses(AF_UNSPEC,
                                       GAA_FLAG_SKIP_ANYCAST |
                                       GAA_FLAG_SKIP_MULTICAST |
                                       GAA_FLAG_SKIP_DNS_SERVER,
                                       NULL, addrs, &len);
            if (ret == NO_ERROR) {
                break;
            }
            free(addrs);
            addrs = NULL;
            if (ret != ERROR_BUFFER_OVERFLOW) {
                return HAL_IO_ERROR;
            }
        }
        if (addrs == NULL) {
            return HAL_IO_ERROR;
        }

        {
            IP_ADAPTER_ADDRESSES *a;

            for (a = addrs; a != NULL; a = a->Next) {
                if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                    a->OperStatus != IfOperStatusUp ||
                    a->PhysicalAddressLength != 6) {
                    continue;
                }
                if (eth_mac_text(a->PhysicalAddress, mac, size) == HAL_OK) {
                    status = HAL_OK;
                    break;
                }
            }
        }
        free(addrs);
        return status;
    }
#elif defined(__linux__)
    {
        struct ifaddrs *list = NULL;
        struct ifaddrs *ifa;
        hal_status_t status = HAL_NOT_FOUND;

        if (getifaddrs(&list) != 0) {
            return HAL_IO_ERROR;
        }

        for (ifa = list; ifa != NULL; ifa = ifa->ifa_next) {
            struct sockaddr_ll *sll;

            if (ifa->ifa_addr == NULL ||
                ifa->ifa_addr->sa_family != AF_PACKET ||
                (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
                continue;
            }
            sll = (struct sockaddr_ll *)ifa->ifa_addr;
            if (sll->sll_halen == 6 &&
                eth_mac_text(sll->sll_addr, mac, size) == HAL_OK) {
                status = HAL_OK;
                break;
            }
        }

        freeifaddrs(list);
        return status;
    }
#else
    (void)mac;
    (void)size;
    return HAL_NOT_SUPPORTED;
#endif
}

hal_status_t iot_net_gettime(iot_net_time_t *time_out)
{
    time_t now;
    struct tm *tm;

    if (time_out == NULL) {
        return HAL_INVALID_PARAM;
    }

    now = time(NULL);
    if (now == (time_t)-1) {
        return HAL_IO_ERROR;
    }
    tm = gmtime(&now);
    if (tm == NULL) {
        return HAL_IO_ERROR;
    }

    time_out->year = tm->tm_year + 1900;
    time_out->month = tm->tm_mon + 1;
    time_out->day = tm->tm_mday;
    time_out->hour = tm->tm_hour;
    time_out->minute = tm->tm_min;
    time_out->second = tm->tm_sec;
    time_out->zone_min = 0;         /* UTC */
    return HAL_OK;
}

#endif /* IOT_NETWORK_ETH */
