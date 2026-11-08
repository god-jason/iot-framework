#ifndef ETH_NET_H
#define ETH_NET_H

/*
 * ETH 平台共享的 Mongoose 事件管理器。
 * HTTP 与 MQTT 客户端共用同一个 mg_mgr，遵循 Mongoose 单管理器约定。
 */

#include <stdbool.h>
#include <stdint.h>

#include "mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 获取共享事件管理器（首次调用时初始化） */
struct mg_mgr *eth_net_mgr(void);

/* 驱动一次事件循环，timeout_ms 为最长等待毫秒数 */
void eth_net_poll(uint32_t timeout_ms);

/* 释放共享事件管理器 */
void eth_net_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* ETH_NET_H */
