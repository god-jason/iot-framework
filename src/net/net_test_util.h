#ifndef NET_TEST_UTIL_H
#define NET_TEST_UTIL_H

/*
 * net_test_util.h - 网络单测用的本地回环服务端工具（仅测试使用）
 *
 * 在调用线程内完成 socket/bind/listen，另起线程 accept 并处理连接；
 * 一个服务端可顺序处理多个连接，ntu_server_stop() 关闭监听并回收线程。
 */

#include "net.h"

#if NET_ENABLE && defined(ROS_PLATFORM_PC)

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET ntu_fd_t;
#define NTU_INVALID_FD   INVALID_SOCKET
#define ntu_close_fd(fd) closesocket(fd)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int ntu_fd_t;
#define NTU_INVALID_FD   (-1)
#define ntu_close_fd(fd) close(fd)
#endif

typedef void (*ntu_conn_fn)(intptr_t fd, void *user);

typedef struct {
    intptr_t    listen_fd;
    ntu_conn_fn fn;
    void       *user;
    bool        started;
#if defined(_WIN32)
    HANDLE      thread;
#else
    pthread_t   thread;
#endif
} ntu_server_t;

#if defined(_WIN32)
static DWORD WINAPI ntu_accept_proc(LPVOID arg)
#else
static void *ntu_accept_proc(void *arg)
#endif
{
    ntu_server_t *srv = (ntu_server_t *)arg;

    for (;;) {
        ntu_fd_t fd = accept((ntu_fd_t)srv->listen_fd, NULL, NULL);

        if (fd == NTU_INVALID_FD) {
            break;
        }
        srv->fn((intptr_t)fd, srv->user);
        ntu_close_fd(fd);
    }
    return 0;
}

static inline bool ntu_server_start(ntu_server_t *srv, uint16_t port,
                                    ntu_conn_fn fn, void *user)
{
    struct sockaddr_in addr;
    ntu_fd_t fd;
    int one = 1;

    if (srv == NULL || fn == NULL) {
        return false;
    }
    memset(srv, 0, sizeof(*srv));
#if defined(_WIN32)
    {
        WSADATA data;

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return false;
        }
    }
#endif
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == NTU_INVALID_FD) {
        return false;
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one,
                     sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 4) != 0) {
        ntu_close_fd(fd);
        return false;
    }

    srv->listen_fd = (intptr_t)fd;
    srv->fn = fn;
    srv->user = user;
    srv->started = true;
#if defined(_WIN32)
    srv->thread = CreateThread(NULL, 0, ntu_accept_proc, srv, 0, NULL);
    if (srv->thread == NULL) {
        ntu_close_fd(fd);
        srv->started = false;
        return false;
    }
#else
    if (pthread_create(&srv->thread, NULL, ntu_accept_proc, srv) != 0) {
        ntu_close_fd(fd);
        srv->started = false;
        return false;
    }
#endif
    return true;
}

static inline void ntu_server_stop(ntu_server_t *srv)
{
    if (srv == NULL || !srv->started) {
        return;
    }
    /* 关闭监听使 accept 返回，再等待线程结束 */
    ntu_close_fd((ntu_fd_t)srv->listen_fd);
#if defined(_WIN32)
    (void)WaitForSingleObject(srv->thread, 5000);
    (void)CloseHandle(srv->thread);
#else
    (void)pthread_join(srv->thread, NULL);
#endif
    srv->started = false;
}

static inline int ntu_send_all(intptr_t fd, const void *data, uint32_t len)
{
    const char *p = (const char *)data;
    uint32_t sent = 0U;

    while (sent < len) {
        int n = (int)send((ntu_fd_t)fd, p + sent, (int)(len - sent), 0);

        if (n <= 0) {
            return -1;
        }
        sent += (uint32_t)n;
    }
    return (int)sent;
}

static inline int ntu_recv_some(intptr_t fd, void *data, uint32_t len)
{
    return (int)recv((ntu_fd_t)fd, (char *)data, (int)len, 0);
}

static inline int ntu_recv_exact(intptr_t fd, void *data, uint32_t len)
{
    uint8_t *p = (uint8_t *)data;
    uint32_t got = 0U;

    while (got < len) {
        int n = ntu_recv_some(fd, &p[got], len - got);

        if (n <= 0) {
            return -1;
        }
        got += (uint32_t)n;
    }
    return (int)got;
}

#endif /* NET_ENABLE && ROS_PLATFORM_PC */

#endif /* NET_TEST_UTIL_H */
