#ifndef HTTP_H
#define HTTP_H

/*
 * http.h - 基于 net 的简易 HTTP/1.1 客户端
 *
 * 只支持：GET、POST（请求体一次性发送）、文件下载（保存到本地路径）。
 * 不支持 HTTPS、分块传输、重定向（状态码原样返回给调用者）。
 *
 * 响应体通过回调流式输出，回调返回非 0 表示主动中止。
 */

#include "net.h"

#ifdef __cplusplus
extern "C" {
#endif

#if NET_ENABLE

#define HTTP_PATH_MAX   192

typedef struct {
    char     host[NET_HOST_MAX];
    uint16_t port;              /* 未指定时为 80 */
    char     path[HTTP_PATH_MAX];
    bool     https;
} http_url_t;

typedef struct {
    int      status;            /* HTTP 状态码，如 200 */
    uint32_t content_length;    /* 0 表示未提供 */
    char     content_type[48];
} http_response_t;

/* 响应体回调：返回 0 继续，非 0 中止 */
typedef int (*http_body_cb)(const uint8_t *data, uint32_t len, void *user);

/* 自定义请求头（name 不含 ':'，value 不含 CR/LF） */
typedef struct {
    const char *name;
    const char *value;
} http_header_t;

/* 解析 URL：仅支持 http://host[:port][/path] */
int http_url_parse(const char *url, http_url_t *out);

/*
 * GET 请求。
 * headers / header_count : 附加请求头，可为 NULL / 0；
 * rsp / cb 均可为 NULL（cb 为 NULL 时丢弃响应体）。
 * 返回 NET_OK 表示完成（HTTP 状态码可能非 200）；NET_ERR_* 表示网络/协议错误。
 */
int http_get(const char *url, const http_header_t *headers,
             uint32_t header_count, http_response_t *rsp, http_body_cb cb,
             void *user, uint32_t timeout_ms);

/* POST 请求，body 一次性发送 */
int http_post(const char *url, const http_header_t *headers,
              uint32_t header_count, const char *content_type,
              const void *body, uint32_t body_len,
              http_response_t *rsp, http_body_cb cb, void *user,
              uint32_t timeout_ms);

/* 下载到文件（PC 等带文件系统的平台）；失败时删除半成品文件 */
int http_download(const char *url, const http_header_t *headers,
                  uint32_t header_count, const char *path,
                  uint32_t timeout_ms);

#endif /* NET_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* HTTP_H */
