/* ETH 网络实现测试：本机信息查询、TCP 回环、UDP 回环。 */
#include "test.h"
#include "iot_net.h"

#include <string.h>

#define ETH_TEST_TCP_PORT 45678U
#define ETH_TEST_UDP_PORT 45679U

void ut_eth_net(void)
{
    iot_net_id_t listener;
    iot_net_id_t client;
    iot_net_id_t server;
    char ip[IOT_NET_IP_MAX];
    char mac[IOT_NET_MAC_MAX];
    iot_net_time_t time;
    uint8_t buf[16];

    tf_begin("eth_net/初始化与本机信息");
    TF_CHECK_INT(iot_net_init(), HAL_OK);
    TF_CHECK_INT(iot_net_getip(ip, sizeof(ip)), HAL_OK);
    TF_CHECK(ip[0] != '\0');
    TF_CHECK_INT(iot_net_gettime(&time), HAL_OK);
    TF_CHECK(time.year >= 2020);
    TF_CHECK_INT(iot_net_gethostbyname("localhost", ip, sizeof(ip)), HAL_OK);
    TF_CHECK_STR(ip, "127.0.0.1");
    tf_end();

    tf_begin("eth_net/本机 MAC");
    TF_CHECK_INT(iot_net_getmac(mac, sizeof(mac)), HAL_OK);
    TF_CHECK_INT((int)strlen(mac), 17);
    tf_end();

    tf_begin("eth_net/TCP 监听、连接与收发");
    TF_CHECK_INT(iot_net_listen(IOT_NET_TCP, "127.0.0.1",
                                ETH_TEST_TCP_PORT, &listener), HAL_OK);
    client = iot_net_connect(IOT_NET_TCP, "127.0.0.1",
                             ETH_TEST_TCP_PORT, 2000U);
    TF_CHECK(client != IOT_NET_INVALID_ID);
    TF_CHECK_INT(iot_net_accept(listener, &server, NULL, 0U, NULL, 2000U),
                 HAL_OK);
    TF_CHECK_INT(iot_net_send(client, (const uint8_t *)"ping", 4U), 4);
    TF_CHECK_INT(iot_net_recv(server, buf, sizeof(buf), 2000U), 4);
    TF_CHECK_MEM(buf, "ping", 4);
    TF_CHECK_INT(iot_net_send(server, (const uint8_t *)"pong", 4U), 4);
    TF_CHECK_INT(iot_net_recv(client, buf, sizeof(buf), 2000U), 4);
    TF_CHECK_MEM(buf, "pong", 4);
    TF_CHECK_INT(iot_net_close(client), HAL_OK);
    TF_CHECK_INT(iot_net_close(server), HAL_OK);
    TF_CHECK_INT(iot_net_close(listener), HAL_OK);
    tf_end();

    tf_begin("eth_net/UDP 监听与收发");
    TF_CHECK_INT(iot_net_listen(IOT_NET_UDP, "127.0.0.1",
                                ETH_TEST_UDP_PORT, &listener), HAL_OK);
    client = iot_net_connect(IOT_NET_UDP, "127.0.0.1",
                             ETH_TEST_UDP_PORT, 2000U);
    TF_CHECK(client != IOT_NET_INVALID_ID);
    TF_CHECK_INT(iot_net_send(client, (const uint8_t *)"udp!", 4U), 4);
    TF_CHECK_INT(iot_net_recvfrom(listener, buf, sizeof(buf), 2000U,
                                  ip, sizeof(ip), NULL), 4);
    TF_CHECK_MEM(buf, "udp!", 4);
    TF_CHECK_INT(iot_net_close(client), HAL_OK);
    TF_CHECK_INT(iot_net_close(listener), HAL_OK);
    tf_end();

    TF_CHECK_INT(iot_net_deinit(), HAL_OK);
}
