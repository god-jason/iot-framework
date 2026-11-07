/*
 * PC 平台网络实现：共享 Mongoose 管理器 + 系统 BSD socket。
 */
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
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

#include "pc_net.h"
#include "iot_net.h"

#if defined(IOT_NETWORK_PC)

#define PC_NET_MAX_CONN          6
#define PC_NET_DEFAULT_TIMEOUT   10000U

#if defined(_WIN32)
typedef SOCKET pc_socket_t;
#define PC_SOCKET_INVALID      INVALID_SOCKET
#define PC_SOCKET_CLOSE        closesocket
#else
typedef int pc_socket_t;
#define PC_SOCKET_INVALID      (-1)
#define PC_SOCKET_CLOSE        close
#endif

typedef struct {
    bool used;
    bool listener;
    bool udp;
    pc_socket_t fd;
} pc_net_socket_t;

static struct mg_mgr s_mgr;
static bool s_ready;

static pc_net_socket_t s_sockets[PC_NET_MAX_CONN];

struct mg_mgr *pc_net_mgr(void)
{
    if (!s_ready) {
        mg_mgr_init(&s_mgr);
        s_ready = true;
    }
    return &s_mgr;
}

void pc_net_poll(uint32_t timeout_ms)
{
    mg_mgr_poll(pc_net_mgr(), (int)timeout_ms);
}

void pc_net_deinit(void)
{
    if (s_ready) {
        mg_mgr_free(&s_mgr);
        s_ready = false;
    }
}

/* ---------------- socket 辅助 ---------------- */

static int pc_socket_set_blocking(pc_socket_t fd, bool blocking)
{
#if defined(_WIN32)
    u_long mode = blocking ? 0 : 1;
    return (ioctlsocket(fd, FIONBIO, &mode) == 0) ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }
    if (blocking) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }
    return (fcntl(fd, F_SETFL, flags) == 0) ? 0 : -1;
#endif
}

static bool pc_socket_in_progress(void)
{
#if defined(_WIN32)
    int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
#else
    return errno == EINPROGRESS;
#endif
}

/* >0 就绪，0 超时，<0 错误 */
static int pc_socket_wait(pc_socket_t fd, bool for_write, uint32_t timeout_ms)
{
    fd_set set;
    struct timeval tv;

    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec = (long)(timeout_ms / 1000U);
    tv.tv_usec = (long)((timeout_ms % 1000U) * 1000U);

#if defined(_WIN32)
    return select(0, for_write ? NULL : &set, for_write ? &set : NULL,
                  NULL, &tv);
#else
    return select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL,
                  NULL, &tv);
#endif
}

static int pc_net_alloc(void)
{
    int i;

    for (i = 0; i < PC_NET_MAX_CONN; i++) {
        if (!s_sockets[i].used) {
            return i;
        }
    }
    return -1;
}

/* ---------------- iot_net 接口实现 ---------------- */

iot_status_t iot_net_init(void)
{
    (void)pc_net_mgr();
    return IOT_OK;
}

iot_status_t iot_net_deinit(void)
{
    int i;

    for (i = 0; i < PC_NET_MAX_CONN; i++) {
        if (s_sockets[i].used) {
            PC_SOCKET_CLOSE(s_sockets[i].fd);
            s_sockets[i].used = false;
        }
    }
    return IOT_OK;
}

iot_net_id_t iot_net_connect(iot_net_type_t type, const char *host,
                             uint16_t port, uint32_t timeout)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    char service[8];
    uint32_t wait = (timeout != 0) ? timeout : PC_NET_DEFAULT_TIMEOUT;
    pc_socket_t fd = PC_SOCKET_INVALID;
    int slot;

    if (host == NULL || host[0] == '\0') {
        return IOT_NET_INVALID_ID;
    }

    (void)pc_net_mgr();

    snprintf(service, sizeof(service), "%u", (unsigned)port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = (type == IOT_NET_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = (type == IOT_NET_UDP) ? IPPROTO_UDP : IPPROTO_TCP;

    if (getaddrinfo(host, service, &hints, &res) != 0) {
        return IOT_NET_INVALID_ID;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == PC_SOCKET_INVALID) {
            continue;
        }

        if (type == IOT_NET_UDP) {
            if (connect(fd, p->ai_addr, (int)p->ai_addrlen) == 0) {
                break;
            }
        } else {
            int rc;

            (void)pc_socket_set_blocking(fd, false);
            rc = connect(fd, p->ai_addr, (int)p->ai_addrlen);
            if (rc == 0) {
                (void)pc_socket_set_blocking(fd, true);
                break;
            }
            if (pc_socket_in_progress()) {
                if (pc_socket_wait(fd, true, wait) > 0) {
                    int err = 0;
#if defined(_WIN32)
                    int err_len = (int)sizeof(err);
#else
                    socklen_t err_len = (socklen_t)sizeof(err);
#endif
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                   (char *)&err, &err_len) == 0 && err == 0) {
                        (void)pc_socket_set_blocking(fd, true);
                        break;
                    }
                }
            }
            (void)pc_socket_set_blocking(fd, true);
        }

        PC_SOCKET_CLOSE(fd);
        fd = PC_SOCKET_INVALID;
    }

    freeaddrinfo(res);

    if (fd == PC_SOCKET_INVALID) {
        return IOT_NET_INVALID_ID;
    }

    slot = pc_net_alloc();
    if (slot < 0) {
        PC_SOCKET_CLOSE(fd);
        return IOT_NET_INVALID_ID;
    }

    s_sockets[slot].used = true;
    s_sockets[slot].listener = false;
    s_sockets[slot].udp = (type == IOT_NET_UDP);
    s_sockets[slot].fd = fd;
    return (iot_net_id_t)slot;
}

int32_t iot_net_send(iot_net_id_t id, const uint8_t *data, uint32_t len)
{
    int32_t sent = 0;

    if (id < 0 || id >= PC_NET_MAX_CONN || !s_sockets[id].used ||
        s_sockets[id].listener || data == NULL || len == 0) {
        return IOT_INVALID_PARAM;
    }

    while ((uint32_t)sent < len) {
        int n = (int)send(s_sockets[id].fd, (const char *)data + sent,
                          (int)(len - (uint32_t)sent), 0);
        if (n <= 0) {
            return IOT_IO_ERROR;
        }
        sent += n;
    }
    return sent;
}

int32_t iot_net_recv(iot_net_id_t id, uint8_t *data, uint32_t len,
                     uint32_t timeout)
{
    int rc;
    int n;

    if (id < 0 || id >= PC_NET_MAX_CONN || !s_sockets[id].used ||
        s_sockets[id].listener || data == NULL || len == 0) {
        return IOT_INVALID_PARAM;
    }

    rc = pc_socket_wait(s_sockets[id].fd, false, timeout);
    if (rc == 0) {
        return 0;
    }
    if (rc < 0) {
        return IOT_IO_ERROR;
    }

    n = (int)recv(s_sockets[id].fd, (char *)data, (int)len, 0);
    if (n == 0) {
        return 0;                   /* 对端关闭 */
    }
    if (n < 0) {
        return IOT_IO_ERROR;
    }
    return (int32_t)n;
}

iot_status_t iot_net_close(iot_net_id_t id)
{
    if (id < 0 || id >= PC_NET_MAX_CONN || !s_sockets[id].used) {
        return IOT_NOT_FOUND;
    }

    PC_SOCKET_CLOSE(s_sockets[id].fd);
    s_sockets[id].used = false;
    s_sockets[id].listener = false;
    s_sockets[id].udp = false;
    return IOT_OK;
}

static void pc_net_peer_text(const struct sockaddr_in *sin, char *ip,
                             uint32_t ip_size, uint16_t *port)
{
    if (ip != NULL && ip_size > 0) {
        const char *text = inet_ntoa(sin->sin_addr);

        if (text != NULL) {
            snprintf(ip, ip_size, "%s", text);
        }
    }
    if (port != NULL) {
        *port = ntohs(sin->sin_port);
    }
}

iot_status_t iot_net_listen(iot_net_type_t type, const char *host,
                            uint16_t port, iot_net_id_t *id)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    char service[8];
    const char *node = (host != NULL && host[0] != '\0') ? host : "0.0.0.0";
    pc_socket_t fd = PC_SOCKET_INVALID;
    int slot;

    if (id == NULL || (type != IOT_NET_TCP && type != IOT_NET_UDP)) {
        return IOT_INVALID_PARAM;
    }
    *id = IOT_NET_INVALID_ID;

    (void)pc_net_mgr();

    snprintf(service, sizeof(service), "%u", (unsigned)port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = (type == IOT_NET_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = (type == IOT_NET_UDP) ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(node, service, &hints, &res) != 0) {
        return IOT_IO_ERROR;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        int yes = 1;

        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == PC_SOCKET_INVALID) {
            continue;
        }
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                         (const char *)&yes, (int)sizeof(yes));
        if (bind(fd, p->ai_addr, (int)p->ai_addrlen) != 0) {
            PC_SOCKET_CLOSE(fd);
            fd = PC_SOCKET_INVALID;
            continue;
        }
        if (type == IOT_NET_TCP && listen(fd, 4) != 0) {
            PC_SOCKET_CLOSE(fd);
            fd = PC_SOCKET_INVALID;
            continue;
        }
        break;
    }

    freeaddrinfo(res);

    if (fd == PC_SOCKET_INVALID) {
        return IOT_IO_ERROR;
    }

    slot = pc_net_alloc();
    if (slot < 0) {
        PC_SOCKET_CLOSE(fd);
        return IOT_NO_MEMORY;
    }

    s_sockets[slot].used = true;
    s_sockets[slot].listener = true;
    s_sockets[slot].udp = (type == IOT_NET_UDP);
    s_sockets[slot].fd = fd;
    *id = (iot_net_id_t)slot;
    return IOT_OK;
}

iot_status_t iot_net_accept(iot_net_id_t listen_id, iot_net_id_t *conn_id,
                            char *peer_ip, uint32_t ip_size,
                            uint16_t *peer_port, uint32_t timeout)
{
    struct sockaddr_in peer;
    pc_socket_t fd;
    int rc;
    int slot;
#if defined(_WIN32)
    int peer_len = (int)sizeof(peer);
#else
    socklen_t peer_len = (socklen_t)sizeof(peer);
#endif

    if (listen_id < 0 || listen_id >= PC_NET_MAX_CONN || conn_id == NULL ||
        !s_sockets[listen_id].used || !s_sockets[listen_id].listener ||
        s_sockets[listen_id].udp || (peer_ip != NULL && ip_size == 0)) {
        return IOT_INVALID_PARAM;
    }
    *conn_id = IOT_NET_INVALID_ID;

    rc = pc_socket_wait(s_sockets[listen_id].fd, false, timeout);
    if (rc == 0) {
        return IOT_TIMEOUT;
    }
    if (rc < 0) {
        return IOT_IO_ERROR;
    }

    memset(&peer, 0, sizeof(peer));
    fd = accept(s_sockets[listen_id].fd, (struct sockaddr *)&peer, &peer_len);
    if (fd == PC_SOCKET_INVALID) {
        return IOT_IO_ERROR;
    }

    slot = pc_net_alloc();
    if (slot < 0) {
        PC_SOCKET_CLOSE(fd);
        return IOT_NO_MEMORY;
    }

    s_sockets[slot].used = true;
    s_sockets[slot].listener = false;
    s_sockets[slot].udp = false;
    s_sockets[slot].fd = fd;
    *conn_id = (iot_net_id_t)slot;

    pc_net_peer_text(&peer, peer_ip, ip_size, peer_port);
    return IOT_OK;
}

int32_t iot_net_sendto(iot_net_id_t id, const char *host, uint16_t port,
                       const uint8_t *data, uint32_t len)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    char service[8];
    int32_t sent = IOT_IO_ERROR;

    if (id < 0 || id >= PC_NET_MAX_CONN || !s_sockets[id].used ||
        !s_sockets[id].udp || host == NULL || data == NULL || len == 0) {
        return IOT_INVALID_PARAM;
    }

    (void)pc_net_mgr();

    snprintf(service, sizeof(service), "%u", (unsigned)port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (getaddrinfo(host, service, &hints, &res) != 0) {
        return IOT_IO_ERROR;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        int n = (int)sendto(s_sockets[id].fd, (const char *)data, (int)len, 0,
                            p->ai_addr, (int)p->ai_addrlen);

        if (n >= 0) {
            sent = (int32_t)n;
            break;
        }
    }

    freeaddrinfo(res);
    return sent;
}

int32_t iot_net_recvfrom(iot_net_id_t id, uint8_t *data, uint32_t len,
                         uint32_t timeout, char *peer_ip, uint32_t ip_size,
                         uint16_t *peer_port)
{
    struct sockaddr_in peer;
    int rc;
    int n;
#if defined(_WIN32)
    int peer_len = (int)sizeof(peer);
#else
    socklen_t peer_len = (socklen_t)sizeof(peer);
#endif

    if (id < 0 || id >= PC_NET_MAX_CONN || !s_sockets[id].used ||
        !s_sockets[id].udp || data == NULL || len == 0 ||
        (peer_ip != NULL && ip_size == 0)) {
        return IOT_INVALID_PARAM;
    }

    rc = pc_socket_wait(s_sockets[id].fd, false, timeout);
    if (rc == 0) {
        return 0;
    }
    if (rc < 0) {
        return IOT_IO_ERROR;
    }

    memset(&peer, 0, sizeof(peer));
    n = (int)recvfrom(s_sockets[id].fd, (char *)data, (int)len, 0,
                      (struct sockaddr *)&peer, &peer_len);
    if (n < 0) {
        return IOT_IO_ERROR;
    }

    pc_net_peer_text(&peer, peer_ip, ip_size, peer_port);
    return (int32_t)n;
}

iot_status_t iot_net_gethostbyname(const char *name, char *ip, uint32_t size)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    iot_status_t status = IOT_NOT_FOUND;

    if (name == NULL || ip == NULL || size == 0) {
        return IOT_INVALID_PARAM;
    }

    (void)pc_net_mgr();

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(name, NULL, &hints, &res) != 0) {
        return IOT_IO_ERROR;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *sin = (struct sockaddr_in *)p->ai_addr;
        const char *text = inet_ntoa(sin->sin_addr);

        if (text != NULL) {
            snprintf(ip, size, "%s", text);
            status = IOT_OK;
            break;
        }
    }

    freeaddrinfo(res);
    return status;
}

iot_status_t iot_net_getip(char *ip, uint32_t size)
{
    char host[256];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;

    if (ip == NULL || size == 0) {
        return IOT_INVALID_PARAM;
    }

    (void)pc_net_mgr();

    if (gethostname(host, (int)sizeof(host)) != 0) {
        return IOT_IO_ERROR;
    }
    host[sizeof(host) - 1] = '\0';

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        return IOT_IO_ERROR;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *sin = (struct sockaddr_in *)p->ai_addr;
        const char *text = inet_ntoa(sin->sin_addr);

        if (text != NULL) {
            snprintf(ip, size, "%s", text);
            freeaddrinfo(res);
            return IOT_OK;
        }
    }

    freeaddrinfo(res);
    return IOT_NOT_FOUND;
}

static iot_status_t pc_net_mac_text(const uint8_t *addr, char *mac, uint32_t size)
{
    if (size < IOT_NET_MAC_MAX) {
        return IOT_OVERFLOW;
    }
    snprintf(mac, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    return IOT_OK;
}

iot_status_t iot_net_getmac(char *mac, uint32_t size)
{
    if (mac == NULL) {
        return IOT_INVALID_PARAM;
    }
    if (size < IOT_NET_MAC_MAX) {
        return IOT_OVERFLOW;
    }

#if defined(_WIN32)
    {
        IP_ADAPTER_ADDRESSES *addrs = NULL;
        ULONG len = 15000;
        ULONG ret;
        iot_status_t status = IOT_NOT_FOUND;
        int attempt;

        for (attempt = 0; attempt < 3; attempt++) {
            addrs = (IP_ADAPTER_ADDRESSES *)malloc(len);
            if (addrs == NULL) {
                return IOT_NO_MEMORY;
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
                return IOT_IO_ERROR;
            }
        }
        if (addrs == NULL) {
            return IOT_IO_ERROR;
        }

        {
            IP_ADAPTER_ADDRESSES *a;

            for (a = addrs; a != NULL; a = a->Next) {
                if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                    a->OperStatus != IfOperStatusUp ||
                    a->PhysicalAddressLength != 6) {
                    continue;
                }
                if (pc_net_mac_text(a->PhysicalAddress, mac, size) == IOT_OK) {
                    status = IOT_OK;
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
        iot_status_t status = IOT_NOT_FOUND;

        if (getifaddrs(&list) != 0) {
            return IOT_IO_ERROR;
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
                pc_net_mac_text(sll->sll_addr, mac, size) == IOT_OK) {
                status = IOT_OK;
                break;
            }
        }

        freeifaddrs(list);
        return status;
    }
#else
    (void)mac;
    (void)size;
    return IOT_NOT_SUPPORTED;
#endif
}

iot_status_t iot_net_gettime(iot_net_time_t *time_out)
{
    time_t now;
    struct tm *tm;

    if (time_out == NULL) {
        return IOT_INVALID_PARAM;
    }

    now = time(NULL);
    if (now == (time_t)-1) {
        return IOT_IO_ERROR;
    }
    tm = gmtime(&now);
    if (tm == NULL) {
        return IOT_IO_ERROR;
    }

    time_out->year = tm->tm_year + 1900;
    time_out->month = tm->tm_mon + 1;
    time_out->day = tm->tm_mday;
    time_out->hour = tm->tm_hour;
    time_out->minute = tm->tm_min;
    time_out->second = tm->tm_sec;
    time_out->zone_min = 0;         /* UTC */
    return IOT_OK;
}

#endif /* IOT_NETWORK_PC */
