#ifndef NET_H
#define NET_H

/*
 * net.h - 网络封装（系统 BSD socket 或 lwIP）
 *
 * 提供两层接口：
 *   1) 原始接口：net_open / net_send / net_recv / net_close；
 *   2) 流式接口：net_stream_*，带收发缓冲，读写更接近文件语义；
 *      并通过 net_io_bind() 绑定成 ros_io_t，可直接作为任务 stdio 使用。
 *
 * 编译方式：
 *   后端由构建系统选择参与编译的文件，不再使用后端宏：
 *   - 主机（PC）：components/net/net_bsd.c（Winsock / POSIX socket）；
 *   - ML307：modules/ml307/ml307_net.c（AT 指令）；
 *   - lwIP：按 BSD 路径编译 net_bsd.c（定义 NET_USE_LWIP）。
 *   components/net/net_stream.c 为通用流式层，所有后端共用。
 *
 * 约定：
 *   - 对象由调用者分配，内部不申请堆内存；
 *   - 返回 0 / 正数表示成功，负值为 NET_ERR_*；
 *   - recv / stream_read 的 0 表示超时（无数据），NET_ERR_CLOSED 表示对端关闭。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "ros.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 接口始终可见；具体实现由构建系统选择编译哪个后端文件 */
#define NET_ENABLE          1

/* 错误码 */
#define NET_OK              0
#define NET_ERR_PARAM       (-1)
#define NET_ERR_IO          (-2)
#define NET_ERR_TIMEOUT     (-3)
#define NET_ERR_CLOSED      (-4)
#define NET_ERR_NOT_SUPPORTED (-5)
#define NET_ERR_NOMEM       (-6)

#define NET_HOST_MAX        96
#define NET_STREAM_TX_MAX   256
#define NET_STREAM_RX_MAX   256
#define NET_DEFAULT_TIMEOUT 5000U

typedef enum {
    NET_TCP = 0,
    NET_UDP
} net_type_t;

/* 原始连接（fd 为 SOCKET / int，用 intptr_t 兼容 64 位） */
typedef struct {
    intptr_t fd;
    uint8_t  type;
    bool     used;
} net_sock_t;

/* 流式连接：带发送 / 接收缓冲 */
typedef struct {
    net_sock_t *sock;
    uint32_t    timeout_ms;
    uint32_t    tx_len;
    uint32_t    rx_pos;
    uint32_t    rx_len;
    uint8_t     tx[NET_STREAM_TX_MAX];
    uint8_t     rx[NET_STREAM_RX_MAX];
} net_stream_t;

#if NET_ENABLE

/* 全局初始化 / 反初始化（Windows 下为 WSAStartup/WSACleanup） */
int net_init(void);
int net_deinit(void);

/* 注册 net 相关命令（ping）到 cmd 全局命令表；由 net_init 调用 */
void net_register_commands(void);

/*
 * 建立连接。
 * type    : NET_TCP / NET_UDP（UDP 会 connect 到对端，可直接 send/recv）；
 * host    : 域名或 IPv4 字符串；
 * port    : 端口；
 * timeout : 连接超时毫秒数，0 表示 NET_DEFAULT_TIMEOUT。
 * 返回    : NET_OK；失败返回 NET_ERR_*。
 */
int net_open(net_sock_t *sock, net_type_t type, const char *host,
             uint16_t port, uint32_t timeout_ms);

/* 发送全部数据；返回实际发送字节数或 NET_ERR_* */
int net_send(net_sock_t *sock, const void *data, uint32_t len);

/*
 * 接收数据。
 * 返回 >0 字节数；0 超时；NET_ERR_CLOSED 对端关闭；其他负值错误。
 */
int net_recv(net_sock_t *sock, void *data, uint32_t len, uint32_t timeout_ms);

/* 关闭连接（可重复调用） */
int net_close(net_sock_t *sock);

bool net_is_open(const net_sock_t *sock);

/* ---- 流式接口 ---- */

int net_stream_init(net_stream_t *st, net_sock_t *sock, uint32_t timeout_ms);

/* 写入流缓冲；缓冲满时自动 flush。返回写入字节数或 NET_ERR_* */
int net_stream_write(net_stream_t *st, const void *data, uint32_t len);

/* 立即发送缓冲区中的数据 */
int net_stream_flush(net_stream_t *st);

/*
 * 读取流数据：缓冲区有数据立即返回；否则阻塞到超时。
 * 返回 >0 字节数；0 超时；NET_ERR_CLOSED 对端关闭。
 */
int net_stream_read(net_stream_t *st, void *data, uint32_t len);

/* flush 后关闭底层连接 */
int net_stream_close(net_stream_t *st);

/* 用流填充 ros_io_t：read/write/close 全部转发到 net_stream_* */
void net_io_bind(ros_io_t *io, net_stream_t *st);

/* ---- DNS ---- */

/*
 * 域名解析（IPv4 点分十进制）。
 * host : 域名或 IP 字符串；
 * ip   : 输出缓冲，建议不小于 16 字节；
 * size : 缓冲容量。
 * 返回 : NET_OK；NET_ERR_PARAM；NET_ERR_IO。
 */
int net_dns_resolve(const char *host, char *ip, uint32_t size);

/* ---- ping ---- */

/* 单次 ping 结果回调：seq 从 1 开始，ok=false 表示超时 / 不可达 */
typedef void (*net_ping_cb)(uint32_t seq, uint32_t rtt_ms, bool ok, void *user);

typedef struct {
    uint32_t sent;
    uint32_t received;
    uint32_t min_ms;
    uint32_t max_ms;
    uint32_t avg_ms;
} net_ping_stat_t;

/*
 * ICMP echo（ping）。
 * host        : 域名或 IPv4 字符串；
 * count       : 次数（>=1）；
 * interval_ms : 两次间隔，0 表示 1000ms；
 * timeout_ms  : 单次超时，0 表示 1000ms；
 * cb / user   : 每次结果回调，可空；
 * stat        : 汇总输出，可空。
 * 返回        : NET_OK 执行完成（允许丢包）；NET_ERR_NOT_SUPPORTED（lwIP 等
 *               无 ICMP socket 的移植）；NET_ERR_PARAM；NET_ERR_IO。
 */
int net_ping(const char *host, uint32_t count, uint32_t interval_ms,
             uint32_t timeout_ms, net_ping_cb cb, void *user,
             net_ping_stat_t *stat);

#endif /* NET_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* NET_H */
