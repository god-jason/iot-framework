/*
 * ML307 HTTP 实现：使用 at 引擎驱动模组内置 HTTP。
 * 仅实现 GET / POST，响应体写入调用者提供的缓冲区。
 */
#include "iot_http.h"
#include "at.h"

#if defined(IOT_NETWORK_ML307)

#include <stdio.h>
#include <string.h>

#define ML307_HTTP_RESP_MAX 512

static char s_http_resp[ML307_HTTP_RESP_MAX];

static iot_status_t ml307_http_copy(iot_http_response_t *response,
                                    const char *data, uint32_t len)
{
    uint32_t n;

    response->status_code = 0;          /* 模组 AT 原语不返回 HTTP 状态码 */
    response->content_length = len;
    response->body_len = 0;

    if (response->body == NULL || response->body_cap == 0) {
        return IOT_OK;
    }

    n = (len < response->body_cap) ? len : response->body_cap;
    memcpy(response->body, data, n);
    response->body_len = n;
    return (n < len) ? IOT_OVERFLOW : IOT_OK;
}

static iot_status_t ml307_http_read(iot_http_response_t *response)
{
    const char *r;

    if (at_exec("AT+HTTPREAD=0,1024", 5000) != AT_RESULT_OK) {
        return IOT_IO_ERROR;
    }
    r = at_get_response();
    if (r == NULL) {
        return IOT_IO_ERROR;
    }

    strncpy(s_http_resp, r, sizeof(s_http_resp) - 1);
    s_http_resp[sizeof(s_http_resp) - 1] = '\0';
    return ml307_http_copy(response, s_http_resp,
                           (uint32_t)strlen(s_http_resp));
}

static iot_status_t ml307_http_do_get(const iot_http_request_t *request,
                                      iot_http_response_t *response)
{
    char cmd[320];

    (void)at_exec("AT+HTTPTERM", 2000);

    snprintf(cmd, sizeof(cmd), "AT+HTTPGET=%s", request->url);
    if (at_exec(cmd, 10000) != AT_RESULT_OK) {
        return IOT_IO_ERROR;
    }
    return ml307_http_read(response);
}

static iot_status_t ml307_http_do_post(const iot_http_request_t *request,
                                       iot_http_response_t *response)
{
    char cmd[320];
    at_request_t phase;

    (void)at_exec("AT+HTTPTERM", 2000);
    (void)at_exec("AT+HTTPHEADER=\"Content-Type: application/json\"", 2000);

    snprintf(cmd, sizeof(cmd), "AT+HTTPPOST=%s", request->url);

    /* 阶段1：发送 POST 并等待 > 提示符 */
    memset(&phase, 0, sizeof(phase));
    phase.command = cmd;
    phase.success = ">";
    phase.failure = "ERROR";
    phase.timeout = 5000;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return IOT_IO_ERROR;
    }

    /* 阶段2：发送请求体 */
    if (request->body != NULL && request->body_len > 0) {
        at_send_raw(request->body, request->body_len);
    }
    at_send_raw((const uint8_t *)"\r\n", 2);

    /* 阶段3：等待 OK */
    memset(&phase, 0, sizeof(phase));
    phase.command = NULL;
    phase.success = "OK";
    phase.failure = "ERROR";
    phase.timeout = 5000;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return IOT_IO_ERROR;
    }

    return ml307_http_read(response);
}

/* ---------------- iot_http 接口实现 ---------------- */

iot_status_t iot_http_init(void)
{
    return IOT_OK;
}

iot_status_t iot_http_deinit(void)
{
    return IOT_OK;
}

iot_status_t iot_http_request(const iot_http_request_t *request,
                              iot_http_response_t *response)
{
    if (request == NULL || response == NULL || request->url == NULL ||
        request->url[0] == '\0') {
        return IOT_INVALID_PARAM;
    }
    if (response->body_cap > 0 && response->body == NULL) {
        return IOT_INVALID_PARAM;
    }

    response->status_code = 0;
    response->content_length = 0;
    response->body_len = 0;

    switch (request->method) {
    case IOT_HTTP_GET:
        return ml307_http_do_get(request, response);
    case IOT_HTTP_POST:
        return ml307_http_do_post(request, response);
    default:
        return IOT_NOT_SUPPORTED;
    }
}

#endif /* IOT_NETWORK_ML307 */
