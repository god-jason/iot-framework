#ifndef IOT_AT_URC_H
#define IOT_AT_URC_H

/*
 * URC（非请求上报）注册与分发。
 * 以行前缀匹配，与普通 AT 命令事务相互独立。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*iot_at_urc_fn)(const char *line, uint32_t len, void *user);

int iot_at_urc_register(const char *prefix, iot_at_urc_fn callback, void *user);
int iot_at_urc_unregister(const char *prefix);

/* 若 line 匹配某 URC 前缀则调用其回调，返回 IOT_OK；否则 IOT_ERR_NOT_FOUND */
int iot_at_urc_dispatch(const char *line, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_AT_URC_H */