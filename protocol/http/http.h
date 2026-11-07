#ifndef HTTP_H
#define HTTP_H

/*
 * 通用 HTTP/1.1 客户端（与具体传输后端无关）。
 * 通过 http_transport_t 抽象底层连接（TCP / AT 模组 / TLS 均可）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *url;
    const char *method;

    const char *headers;              /* 附加请求头（可多行，以 \r\n 分隔，不含结尾空行） */
    const uint8_t *body;
    uint32_t body_len;

    uint32_t timeout;                 /* 读超时 ms */
} http_request_t;

typedef struct {
    uint16_t status_code;

    const uint8_t *body;              /* 指向实现内部缓冲 */
    uint32_t body_len;

    uint32_t content_length;
} http_response_t;

typedef int (*http_data_fn)(const uint8_t *data, uint32_t len, void *user);

/* 传输后端：connect 到 host:port，write/read 收发字节。
 * read 返回 >0 = 实际读取字节数，0 = 关闭/超时/无数据，<0 = 错误。 */
typedef struct {
    int (*connect)(void *ctx, const char *host, uint16_t port, uint32_t timeout);
    int (*write)(void *ctx, const uint8_t *data, uint32_t len);
    int (*read)(void *ctx, uint8_t *data, uint32_t len, uint32_t timeout);
    void (*close)(void *ctx);
    void *ctx;
} http_transport_t;

/* 解析 URL：拆出 host / port / path（path 至少 "/"）。返回 0 成功。 */
int http_parse_url(const char *url, char *host, uint32_t host_size,
                   uint16_t *port, char *path, uint32_t path_size);

/* 执行一次 HTTP 请求（阻塞），解析状态行 / 头部 / 响应体到 response。
 * 返回 0 成功，负值为错误码。 */
int http_execute(const http_transport_t *transport,
                 const http_request_t *request,
                 http_response_t *response);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_H */