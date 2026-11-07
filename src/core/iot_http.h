#ifndef IOT_HTTP_H
#define IOT_HTTP_H

/*
 * HTTP 客户端接口（单网络实现）。
 *
 * 本文件只定义类型与接口，不做运行期后端注册；
 * 具体实现在 networks/ 下按目标选择性编译：
 *   - networks/pc    : 基于系统 socket，使用 Mongoose 实现；
 *   - networks/ml307 : 基于 AT 指令，使用模组内置 HTTP 能力。
 *
 * 约定：
 *   - 同一目标只编译一个网络实现；
 *   - 响应体缓冲区由调用者提供，实现不申请堆内存。
 */

#include "iot_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* HTTP 请求方法 */
typedef enum {
    IOT_HTTP_GET = 0,          /* GET */
    IOT_HTTP_POST,             /* POST */
    IOT_HTTP_PUT,              /* PUT */
    IOT_HTTP_DELETE,           /* DELETE */
    IOT_HTTP_PATCH,            /* PATCH */
    IOT_HTTP_HEAD              /* HEAD（无响应体） */
} iot_http_method_t;

/* HTTP 请求 */
typedef struct {
    const char *url;            /* 完整 URL，必填，如 "http://host/path" */
    iot_http_method_t method;   /* 请求方法 */
    const char *headers;        /* 附加请求头，多行以 \r\n 分隔，可为 NULL */
    const uint8_t *body;        /* 请求体，可为 NULL */
    uint32_t body_len;          /* 请求体长度（字节） */
    uint32_t timeout;           /* 超时毫秒数，0 表示由实现选择默认值 */
} iot_http_request_t;

/* HTTP 响应 */
typedef struct {
    uint16_t status_code;       /* HTTP 状态码，0 表示后端未提供 */
    uint32_t content_length;    /* 响应头中的 Content-Length，未知为 0 */

    uint8_t *body;              /* 响应体缓冲区，由调用者提供，可为 NULL */
    uint32_t body_cap;          /* 响应体缓冲区容量 */
    uint32_t body_len;          /* 实际写入的响应体长度 */
} iot_http_response_t;

/* 初始化 / 反初始化 HTTP 实现 */
iot_status_t iot_http_init(void);
iot_status_t iot_http_deinit(void);

/*
 * 执行一次阻塞 HTTP 请求。
 * response->body/body_cap 需由调用者预先设置；body 可为 NULL 表示丢弃响应体。
 * 返回：IOT_OK；IOT_INVALID_PARAM；IOT_NOT_READY；IOT_TIMEOUT；
 *       IOT_NOT_SUPPORTED；IOT_OVERFLOW（响应体超过缓冲区）；IOT_IO_ERROR。
 */
iot_status_t iot_http_request(const iot_http_request_t *request,
                              iot_http_response_t *response);

#ifdef __cplusplus
}
#endif

#endif /* IOT_HTTP_H */
