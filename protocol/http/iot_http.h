#ifndef IOT_HTTP_H
#define IOT_HTTP_H

/*
 * 通用 HTTP 请求/响应数据结构（与具体后端无关）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *url;
    const char *method;

    const char *headers;
    const uint8_t *body;
    uint32_t body_len;

    uint32_t timeout;
} iot_http_request_t;

typedef struct {
    uint16_t status_code;

    const uint8_t *body;
    uint32_t body_len;

    uint32_t content_length;
} iot_http_response_t;

typedef int (*iot_http_data_fn)(const uint8_t *data, uint32_t len, void *user);

#ifdef __cplusplus
}
#endif

#endif /* IOT_HTTP_H */