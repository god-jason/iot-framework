/*
 * net_bsd.c - BSD socket 后端的原始接口（PC / lwIP）
 *
 * 提供 net_init / net_open / net_send / net_recv / net_close 与
 * net_dns_resolve / net_ping；流式接口在 net_stream.c 中复用这些原语。
 * 非阻塞 connect + select 实现超时。
 *
 * 本文件仅在主机（或使用 lwIP 的目标）参与编译；
 * ML307 目标改用 modules/ml307/ml307_net.c 提供同样的原始接口。
 */

#include "net.h"


#include <stdio.h>
#include <string.h>


#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
typedef SOCKET net_fd_t;
#define NET_INVALID_FD   INVALID_SOCKET
#define net_close_fd(fd) closesocket(fd)
#define NET_WOULD_BLOCK  WSAEWOULDBLOCK
#define NET_IN_PROGRESS  WSAEWOULDBLOCK
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#if defined(NET_USE_LWIP) || defined(ROS_USE_LWIP)
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#else
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <time.h>
#endif
typedef int net_fd_t;
#define NET_INVALID_FD   (-1)
#define net_close_fd(fd) close(fd)
#define NET_WOULD_BLOCK  EWOULDBLOCK
#define NET_IN_PROGRESS  EINPROGRESS
#endif

static int s_net_ref;

/* ---------------- 基础工具 ---------------- */

static int net_set_nonblock(net_fd_t fd, bool on)
{
#if defined(_WIN32)
    u_long mode = on ? 1UL : 0UL;

    return (ioctlsocket(fd, FIONBIO, &mode) == 0) ? NET_OK : NET_ERR_IO;
#else
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return NET_ERR_IO;
    }
    if (on) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return (fcntl(fd, F_SETFL, flags) == 0) ? NET_OK : NET_ERR_IO;
#endif
}

static int net_last_error(void)
{
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

/* 等待 fd 可读 / 可写，返回 1 就绪，0 超时，-1 错误 */
static int net_wait(net_fd_t fd, bool for_write, uint32_t timeout_ms)
{
    fd_set set;
    struct timeval tv;
    int rc;

    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec = (long)(timeout_ms / 1000U);
    tv.tv_usec = (long)((timeout_ms % 1000U) * 1000U);
    rc = select((int)fd + 1, for_write ? NULL : &set,
                for_write ? &set : NULL, NULL, &tv);
    return (rc > 0) ? 1 : ((rc == 0) ? 0 : -1);
}

/* ---------------- 生命周期 ---------------- */

int net_init(void)
{
    if (s_net_ref == 0) {
#if defined(_WIN32)
        WSADATA data;

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return NET_ERR_IO;
        }
#elif !defined(NET_USE_LWIP) && !defined(ROS_USE_LWIP)
        (void)signal(SIGPIPE, SIG_IGN);
#endif
    }
    s_net_ref++;
    return NET_OK;
}

int net_deinit(void)
{
    if (s_net_ref > 0) {
        s_net_ref--;
#if defined(_WIN32)
        if (s_net_ref == 0) {
            (void)WSACleanup();
        }
#endif
    }
    return NET_OK;
}

/* ---------------- 原始接口 ---------------- */

int net_open(net_sock_t *sock, net_type_t type, const char *host,
             uint16_t port, uint32_t timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    char port_text[8];
    net_fd_t fd = NET_INVALID_FD;
    int rc = NET_ERR_IO;

    if (sock == NULL || host == NULL || host[0] == '\0') {
        return NET_ERR_PARAM;
    }
    memset(sock, 0, sizeof(*sock));
    sock->fd = (intptr_t)NET_INVALID_FD;
    if (net_init() != NET_OK) {
        return NET_ERR_IO;
    }
    if (timeout_ms == 0U) {
        timeout_ms = NET_DEFAULT_TIMEOUT;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = (type == NET_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    if (getaddrinfo(host, port_text, &hints, &res) != 0) {
        return NET_ERR_IO;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == NET_INVALID_FD) {
            continue;
        }
        if (type == NET_TCP) {
            int one = 1;

            (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                             (const char *)&one, sizeof(one));
        }
        (void)net_set_nonblock(fd, true);
        if (connect(fd, p->ai_addr, (int)p->ai_addrlen) == 0) {
            rc = NET_OK;
        } else if (net_last_error() == NET_IN_PROGRESS ||
                   net_last_error() == NET_WOULD_BLOCK) {
            int ready = net_wait(fd, true, timeout_ms);

            if (ready == 1) {
                int err = 0;
                socklen_t elen = (socklen_t)sizeof(err);

                (void)getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                 (char *)&err, &elen);
                rc = (err == 0) ? NET_OK : NET_ERR_IO;
            } else {
                rc = (ready == 0) ? NET_ERR_TIMEOUT : NET_ERR_IO;
            }
        }
        if (rc == NET_OK) {
            break;
        }
        (void)net_close_fd(fd);
        fd = NET_INVALID_FD;
    }
    freeaddrinfo(res);

    if (rc != NET_OK) {
        return rc;
    }
    (void)net_set_nonblock(fd, false);
    sock->fd = (intptr_t)fd;
    sock->type = (uint8_t)type;
    sock->used = true;
    return NET_OK;
}

bool net_is_open(const net_sock_t *sock)
{
    return (sock != NULL && sock->used &&
            (net_fd_t)sock->fd != NET_INVALID_FD);
}

int net_send(net_sock_t *sock, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sent = 0U;

    if (!net_is_open(sock) || (data == NULL && len != 0U)) {
        return NET_ERR_PARAM;
    }
    while (sent < len) {
        int n = (int)send((net_fd_t)sock->fd, (const char *)p + sent,
                          (int)(len - sent), 0);

        if (n > 0) {
            sent += (uint32_t)n;
            continue;
        }
        if (n < 0 && (net_last_error() == NET_WOULD_BLOCK)) {
            if (net_wait((net_fd_t)sock->fd, true, NET_DEFAULT_TIMEOUT) != 1) {
                return NET_ERR_TIMEOUT;
            }
            continue;
        }
        return NET_ERR_IO;
    }
    return (int)sent;
}

int net_recv(net_sock_t *sock, void *data, uint32_t len, uint32_t timeout_ms)
{
    int ready;
    int n;

    if (!net_is_open(sock) || data == NULL || len == 0U) {
        return NET_ERR_PARAM;
    }
    ready = net_wait((net_fd_t)sock->fd, false, timeout_ms);
    if (ready == 0) {
        return 0;
    }
    if (ready < 0) {
        return NET_ERR_IO;
    }
    n = (int)recv((net_fd_t)sock->fd, (char *)data, (int)len, 0);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        return NET_ERR_CLOSED;
    }
    if (net_last_error() == NET_WOULD_BLOCK) {
        return 0;
    }
    return NET_ERR_IO;
}

int net_close(net_sock_t *sock)
{
    if (sock == NULL) {
        return NET_ERR_PARAM;
    }
    if (sock->used && (net_fd_t)sock->fd != NET_INVALID_FD) {
        (void)net_close_fd((net_fd_t)sock->fd);
    }
    sock->fd = (intptr_t)NET_INVALID_FD;
    sock->used = false;
    return NET_OK;
}


/* ---------------- DNS ---------------- */


int net_dns_resolve(const char *host, char *ip, uint32_t size)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    int rc = NET_ERR_IO;

    if (host == NULL || host[0] == '\0' || ip == NULL || size == 0U) {
        return NET_ERR_PARAM;
    }
    if (net_init() != NET_OK) {
        return NET_ERR_IO;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        return NET_ERR_IO;
    }
    for (p = res; p != NULL; p = p->ai_next) {
        const struct sockaddr_in *sin =
            (const struct sockaddr_in *)p->ai_addr;
        const char *text = inet_ntoa(sin->sin_addr);

        if (text != NULL) {
            (void)snprintf(ip, size, "%s", text);
            rc = NET_OK;
            break;
        }
    }
    freeaddrinfo(res);
    return rc;
}


/* ---------------- ping ---------------- */


#define NET_PING_DEFAULT_MS   1000U

#if defined(_WIN32) || (!defined(NET_USE_LWIP) && !defined(ROS_USE_LWIP))

static void net_delay_ms(uint32_t ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)((ms % 1000U) * 1000000U);
    (void)nanosleep(&ts, NULL);
#endif
}

#if defined(_WIN32)

static int net_ping_once(const char *ip, uint32_t seq, uint32_t timeout_ms,
                         uint32_t *rtt_ms)
{
    HANDLE handle;
    IPAddr addr;
    uint8_t payload[32];
    uint8_t reply_buf[sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 8U];
    ICMP_ECHO_REPLY *reply = (ICMP_ECHO_REPLY *)reply_buf;
    DWORD n;

    handle = IcmpCreateFile();
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    addr = inet_addr(ip);
    if (addr == INADDR_NONE) {
        IcmpCloseHandle(handle);
        return -1;
    }
    memset(payload, (int)(0x5A ^ (uint8_t)seq), sizeof(payload));
    memset(reply_buf, 0, sizeof(reply_buf));
    n = IcmpSendEcho(handle, addr, payload, (WORD)sizeof(payload), NULL,
                     reply_buf, (DWORD)sizeof(reply_buf), (DWORD)timeout_ms);
    if (n > 0 && reply->Status == IP_SUCCESS) {
        *rtt_ms = reply->RoundTripTime;
        IcmpCloseHandle(handle);
        return 0;
    }
    IcmpCloseHandle(handle);
    return -1;
}

#else /* !_WIN32 */

static uint64_t net_now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)(ts.tv_nsec / 1000000L);
}

static uint16_t net_icmp_sum(const void *data, uint32_t len)
{
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0U;

    while (len > 1U) {
        sum += *p++;
        len -= 2U;
    }
    if (len != 0U) {
        sum += *(const uint8_t *)p;
    }
    while ((sum >> 16) != 0U) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static int net_ping_once(const char *ip, uint32_t seq, uint32_t timeout_ms,
                         uint32_t *rtt_ms)
{
    struct sockaddr_in dst;
    struct icmphdr req;
    uint16_t ident = (uint16_t)((getpid() ^ (int)seq) & 0xFFFF);
    uint64_t start;
    int fd;
    bool raw = false;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) {
        fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        raw = true;
    }
    if (fd < 0) {
        return -1;
    }
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(ip);
    if (dst.sin_addr.s_addr == INADDR_NONE) {
        (void)close(fd);
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.type = ICMP_ECHO;
    req.code = 0;
    req.un.echo.id = htons(ident);
    req.un.echo.sequence = htons((uint16_t)seq);
    req.checksum = net_icmp_sum(&req, sizeof(req));

    start = net_now_ms();
    if (sendto(fd, &req, sizeof(req), 0, (struct sockaddr *)&dst,
               sizeof(dst)) < 1) {
        (void)close(fd);
        return -1;
    }

    for (;;) {
        uint8_t buf[256];
        struct icmphdr *rep = (struct icmphdr *)buf;
        uint64_t elapsed = net_now_ms() - start;
        uint32_t remain = (elapsed < timeout_ms)
                          ? (uint32_t)(timeout_ms - elapsed) : 0U;
        struct timeval tv;
        fd_set rfds;
        ssize_t n;

        if (remain == 0U) {
            break;
        }
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = (long)(remain / 1000U);
        tv.tv_usec = (long)((remain % 1000U) * 1000U);
        if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
            break;
        }
        n = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n <= 0) {
            break;
        }
        if (raw) {
            const struct iphdr *iph = (const struct iphdr *)buf;
            uint32_t hlen = (uint32_t)iph->ihl * 4U;

            if (n < (ssize_t)(hlen + sizeof(struct icmphdr))) {
                continue;
            }
            rep = (struct icmphdr *)(buf + hlen);
            if (ntohs(rep->un.echo.id) != ident) {
                continue;
            }
        }
        if (rep->type != ICMP_ECHOREPLY ||
            ntohs(rep->un.echo.sequence) != (uint16_t)seq) {
            continue;
        }
        *rtt_ms = (uint32_t)(net_now_ms() - start);
        (void)close(fd);
        return 0;
    }
    (void)close(fd);
    return -1;
}

#endif /* _WIN32 */

int net_ping(const char *host, uint32_t count, uint32_t interval_ms,
             uint32_t timeout_ms, net_ping_cb cb, void *user,
             net_ping_stat_t *stat)
{
    char ip[32];
    uint32_t received = 0U;
    uint32_t min_ms = 0U;
    uint32_t max_ms = 0U;
    uint32_t sum_ms = 0U;
    uint32_t i;

    if (host == NULL || host[0] == '\0' || count == 0U) {
        return NET_ERR_PARAM;
    }
    if (interval_ms == 0U) {
        interval_ms = NET_PING_DEFAULT_MS;
    }
    if (timeout_ms == 0U) {
        timeout_ms = NET_PING_DEFAULT_MS;
    }
    if (net_dns_resolve(host, ip, sizeof(ip)) != NET_OK) {
        return NET_ERR_IO;
    }

    for (i = 1U; i <= count; i++) {
        uint32_t rtt = 0U;
        bool ok = (net_ping_once(ip, i, timeout_ms, &rtt) == 0);

        if (ok) {
            received++;
            sum_ms += rtt;
            if (received == 1U || rtt < min_ms) {
                min_ms = rtt;
            }
            if (rtt > max_ms) {
                max_ms = rtt;
            }
        }
        if (cb != NULL) {
            cb(i, rtt, ok, user);
        }
        if (i < count) {
            net_delay_ms(interval_ms);
        }
    }
    if (stat != NULL) {
        stat->sent = count;
        stat->received = received;
        stat->min_ms = min_ms;
        stat->max_ms = max_ms;
        stat->avg_ms = (received != 0U) ? (sum_ms / received) : 0U;
    }
    return NET_OK;
}

#else /* lwIP：无 ICMP socket */

int net_ping(const char *host, uint32_t count, uint32_t interval_ms,
             uint32_t timeout_ms, net_ping_cb cb, void *user,
             net_ping_stat_t *stat)
{
    (void)host;
    (void)count;
    (void)interval_ms;
    (void)timeout_ms;
    (void)cb;
    (void)user;
    (void)stat;
    return NET_ERR_NOT_SUPPORTED;
}

#endif /* 有 ICMP 实现 */


