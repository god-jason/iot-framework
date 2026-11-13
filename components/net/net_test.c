/*
 * net_test.c - net 组件回归测试（本地回显服务）
 */

#include "test.h"
#include "net.h"
#include "net_test_util.h"

#include <string.h>

#if NET_ENABLE

#define NET_TEST_PORT   18081U

static void echo_server(intptr_t fd, void *user)
{
    uint8_t buf[128];
    int n;

    (void)user;
    for (;;) {
        n = ntu_recv_some(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        if (ntu_send_all(fd, buf, (uint32_t)n) != n) {
            break;
        }
    }
}

static void ping_cb(uint32_t seq, uint32_t rtt, bool ok, void *user)
{
    int *hits = (int *)user;

    (void)seq;
    (void)rtt;
    if (ok) {
        (*hits)++;
    }
}

void ut_net(void)
{
    net_sock_t sock;
    net_stream_t stream;
    ros_io_t io;
    ntu_server_t srv;
    uint8_t buf[64];
    int n;

    tf_begin("net/原始与流式接口");

    TF_CHECK_INT(net_init(), NET_OK);

    /* 参数与连接失败路径 */
    TF_CHECK_INT(net_open(NULL, NET_TCP, "127.0.0.1", 80U, 100U),
                 NET_ERR_PARAM);
    TF_CHECK_INT(net_open(&sock, NET_TCP, "127.0.0.1", 1U, 300U) != NET_OK,
                 1);
    TF_CHECK_INT(net_send(NULL, "x", 1U), NET_ERR_PARAM);
    TF_CHECK_INT(net_recv(NULL, buf, 1U, 10U), NET_ERR_PARAM);

    TF_CHECK(ntu_server_start(&srv, NET_TEST_PORT, echo_server, NULL));
    TF_CHECK_INT(net_open(&sock, NET_TCP, "127.0.0.1", NET_TEST_PORT, 2000U),
                 NET_OK);
    TF_CHECK(net_is_open(&sock));

    /* 原始接口：发送 / 回显 / 超时 */
    TF_CHECK_INT(net_send(&sock, "abc", 3U), 3);
    TF_CHECK_INT(net_recv(&sock, buf, sizeof(buf), 2000U), 3);
    TF_CHECK(buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c');
    TF_CHECK_INT(net_recv(&sock, buf, sizeof(buf), 50U), 0);

    /* DNS 解析 */
    {
        char ip[32] = { 0 };

        TF_CHECK_INT(net_dns_resolve("127.0.0.1", ip, sizeof(ip)), NET_OK);
        TF_CHECK(strcmp(ip, "127.0.0.1") == 0);
        TF_CHECK_INT(net_dns_resolve("localhost", ip, sizeof(ip)), NET_OK);
        TF_CHECK(strlen(ip) > 0U);
        TF_CHECK_INT(net_dns_resolve(NULL, ip, sizeof(ip)), NET_ERR_PARAM);
    }

    /* ping 回环地址 */
    {
        net_ping_stat_t pstat;
        int hits = 0;

        memset(&pstat, 0, sizeof(pstat));
        TF_CHECK_INT(net_ping("127.0.0.1", 2U, 50U, 500U, ping_cb, &hits,
                              &pstat), NET_OK);
        TF_CHECK_INT(hits, 2);
        TF_CHECK_INT((int)pstat.sent, 2);
        TF_CHECK_INT((int)pstat.received, 2);
        TF_CHECK_INT(net_ping("127.0.0.1", 0U, 0U, 0U, NULL, NULL, NULL),
                     NET_ERR_PARAM);
    }

    /* 流式接口：缓冲 + flush + 读取 */
    TF_CHECK_INT(net_stream_init(&stream, &sock, 2000U), NET_OK);
    TF_CHECK_INT(net_stream_write(&stream, "hello", 5U), 5);
    TF_CHECK_INT(net_stream_flush(&stream), NET_OK);
    TF_CHECK_INT(net_stream_read(&stream, buf, sizeof(buf)), 5);
    TF_CHECK(buf[4] == 'o');

    /* ros_io_t 绑定：write → 流缓冲，read → 流读取 */
    net_io_bind(&io, &stream);
    TF_CHECK(io.write != NULL && io.read != NULL && io.close != NULL);
    TF_CHECK_INT(io.write(&io, "xyz", 3U), 3);
    TF_CHECK_INT(net_stream_flush(&stream), NET_OK);
    TF_CHECK_INT(io.read(&io, buf, sizeof(buf)), 3);
    TF_CHECK(buf[0] == 'x' && buf[2] == 'z');

    /* 超过发送缓冲的数据：写满自动 flush，最终全部回显 */
    {
        uint8_t big[600];
        uint32_t total = 0U;

        memset(big, 'q', sizeof(big));
        TF_CHECK_INT(net_stream_write(&stream, big, sizeof(big)), 600);
        TF_CHECK_INT(net_stream_flush(&stream), NET_OK);
        while (total < sizeof(big)) {
            n = net_stream_read(&stream, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            total += (uint32_t)n;
        }
        TF_CHECK_INT((int)total, 600);
    }

    /* 关闭：流关闭会关闭底层连接 */
    TF_CHECK_INT(net_stream_close(&stream), NET_OK);
    TF_CHECK(!net_is_open(&sock));
    TF_CHECK_INT(net_close(&sock), NET_OK);
    ntu_server_stop(&srv);

    TF_CHECK_INT(net_deinit(), NET_OK);
    tf_end();
}

#else

void ut_net(void)
{
    tf_begin("net/原始与流式接口");
    tf_end();
}

#endif /* NET_ENABLE */
