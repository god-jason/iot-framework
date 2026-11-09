#ifndef IOT_NET_H
#define IOT_NET_H

/*
 * 网络 socket 接口（单网络实现）。
 *
 * 本文件只定义类型与接口，不做运行期后端注册；
 * 具体实现在 networks/ 下按目标选择性编译：
 *   - networks/eth    : 系统 BSD socket（TCP/UDP 客户端与服务端）；
 *   - networks/ml307 : AT 指令（MIPOPEN / MIPSEND / MIPURC / MDNSGIP / CCLK）。
 *
 * 约定：
 *   - 同一目标只编译一个网络实现；
 *   - 连接由 iot_net_connect()/iot_net_accept() 分配 ID，
 *     send / recv / close 通过 ID 操作；
 *   - send / recv 返回实际字节数，负值为 IOT_* 错误码；
 *   - recv 返回 0 表示超时或对端关闭。
 */

#include "hal_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* IP 字符串最大长度（含结尾 '\0'） */
#define IOT_NET_IP_MAX       48

/* MAC 字符串最大长度（"aa:bb:cc:dd:ee:ff" + '\0'） */
#define IOT_NET_MAC_MAX      18

/* 无效连接 ID */
#define IOT_NET_INVALID_ID   (-1)

/* 连接类型 */
typedef enum {
    IOT_NET_TCP = 0,        /* 面向连接的 TCP */
    IOT_NET_UDP             /* 数据报 UDP */
} iot_net_type_t;

/* 连接 ID，由 iot_net_connect() 分配 */
typedef int32_t iot_net_id_t;

/* 网络时间 */
typedef struct {
    int year;               /* 完整年份，如 2026 */
    int month;              /* 1..12 */
    int day;                /* 1..31 */
    int hour;               /* 0..23 */
    int minute;             /* 0..59 */
    int second;             /* 0..59 */
    int zone_min;           /* 时区偏移（分钟），东八区为 480 */
} iot_net_time_t;

/* 初始化 / 反初始化网络实现 */
int iot_net_init(void);
int iot_net_deinit(void);

/*
 * 建立连接并分配 ID。
 * type    : IOT_NET_TCP / IOT_NET_UDP；
 * host    : 域名或 IP 字符串，不可为 NULL；
 * port    : 端口；
 * timeout : 连接超时毫秒数，0 表示由实现选择默认值。
 * 返回    : 连接 ID；失败返回 IOT_NET_INVALID_ID。
 */
iot_net_id_t iot_net_connect(iot_net_type_t type, const char *host,
                             uint16_t port, uint32_t timeout);

/* 发送数据，返回实际发送字节数，或负的 IOT_* 错误码 */
int32_t iot_net_send(iot_net_id_t id, const uint8_t *data, uint32_t len);

/* 接收数据，返回字节数；0 = 超时/对端关闭；负值 = 错误码 */
int32_t iot_net_recv(iot_net_id_t id, uint8_t *data, uint32_t len,
                     uint32_t timeout);

/* 关闭连接并释放 ID */
int iot_net_close(iot_net_id_t id);

/*
 * 监听本地端口。
 * type : IOT_NET_TCP 创建 TCP 监听；IOT_NET_UDP 绑定本地 UDP 端口；
 * host : 本地地址，NULL 或 "0.0.0.0" 表示所有网卡；
 * port : 本地端口；
 * id   : 输出监听 ID。
 * 返回 : HAL_OK；HAL_INVALID_PARAM；HAL_NOT_SUPPORTED；HAL_IO_ERROR；HAL_NO_MEMORY。
 */
int iot_net_listen(iot_net_type_t type, const char *host,
                            uint16_t port, iot_net_id_t *id);

/*
 * 接受 TCP 连接。
 * listen_id : iot_net_listen() 返回的 TCP 监听 ID；
 * conn_id   : 输出新连接 ID；
 * peer_ip   : 对端 IP 输出缓冲区，可为 NULL；
 * ip_size   : peer_ip 缓冲区容量；
 * peer_port : 对端端口输出，可为 NULL；
 * timeout   : 等待超时毫秒数，0 表示不等待。
 * 返回      : HAL_OK；HAL_TIMEOUT；HAL_INVALID_PARAM；HAL_NOT_SUPPORTED；HAL_NO_MEMORY。
 */
int iot_net_accept(iot_net_id_t listen_id, iot_net_id_t *conn_id,
                            char *peer_ip, uint32_t ip_size,
                            uint16_t *peer_port, uint32_t timeout);

/*
 * UDP 无连接发送到指定地址。
 * id   : UDP 监听/绑定 ID；
 * host : 目标域名或 IP；
 * port : 目标端口。
 * 返回 : 实际发送字节数，或负的 IOT_* 错误码。
 */
int32_t iot_net_sendto(iot_net_id_t id, const char *host, uint16_t port,
                       const uint8_t *data, uint32_t len);

/*
 * UDP 无连接接收。
 * peer_ip   : 对端 IP 输出缓冲区，可为 NULL；
 * ip_size   : peer_ip 缓冲区容量；
 * peer_port : 对端端口输出，可为 NULL。
 * 返回      : 字节数；0 = 超时；负值 = 错误码。
 */
int32_t iot_net_recvfrom(iot_net_id_t id, uint8_t *data, uint32_t len,
                         uint32_t timeout, char *peer_ip, uint32_t ip_size,
                         uint16_t *peer_port);

/* 域名解析，输出点分十进制 IPv4 字符串 */
int iot_net_gethostbyname(const char *name, char *ip, uint32_t size);

/* 获取本机 IP 地址 */
int iot_net_getip(char *ip, uint32_t size);

/*
 * 获取本机 MAC 地址（小写、冒号分隔）。
 * mac  : 输出缓冲区；
 * size : 缓冲区容量，建议不小于 IOT_NET_MAC_MAX。
 * 返回 : HAL_OK；HAL_INVALID_PARAM；HAL_NOT_SUPPORTED；HAL_OVERFLOW。
 */
int iot_net_getmac(char *mac, uint32_t size);

/* 获取网络时间 */
int iot_net_gettime(iot_net_time_t *time);

#ifdef __cplusplus
}
#endif

#endif /* IOT_NET_H */
