/*
 * HTTP 客户端实现：基于系统 socket，通过 Mongoose HTTP 客户端完成请求。
 * 响应体写入调用者提供的缓冲区，不申请堆内存。
 */
#include "iot_http.h"
#include "pc_net.h"

#if defined(IOT_NETWORK_PC)

#include <stdio.h>
#include <string.h>

#define PC_HTTP_DEFAULT_TIMEOUT_MS 10000U
#define PC_HTTP_POLL_MS            5
#define PC_HTTP_URL_MAX            512

typedef struct {
    bool done;
    bool failed;
    bool overflow;

    const char *method;
    const char *path;
    struct mg_str host;             /* 指向 url，仅在本函数生命周期内有效 */
    const char *headers;
    const uint8_t *body;
    uint32_t body_len;

    struct mg_connection *conn;
    iot_http_response_t *response;
} pc_http_state_t;

static pc_http_state_t s_http;

static const char *pc_http_method(iot_http_method_t method)
{
    switch (method) {
    case IOT_HTTP_POST:
        return "POST";
    case IOT_HTTP_PUT:
        return "PUT";
    case IOT_HTTP_DELETE:
        return "DELETE";
    case IOT_HTTP_PATCH:
        return "PATCH";
    case IOT_HTTP_HEAD:
        return "HEAD";
    case IOT_HTTP_GET:
    default:
        return "GET";
    }
}

/* 补全 scheme，mongoose 的 URL 解析需要显式的协议头 */
static const char *pc_http_url(const char *url, char *buf, size_t size)
{
    if (strstr(url, "://") != NULL) {
        return url;
    }
    if (snprintf(buf, size, "http://%s", url) >= (int)size) {
        return NULL;
    }
    return buf;
}

static uint32_t pc_http_content_length(struct mg_http_message *hm)
{
    struct mg_str *value = mg_http_get_header(hm, "Content-Length");
    uint32_t n = 0;
    size_t i;

    if (value == NULL) {
        return 0;
    }
    for (i = 0; i < value->len && value->buf[i] >= '0' && value->buf[i] <= '9'; i++) {
        n = n * 10U + (uint32_t)(value->buf[i] - '0');
    }
    return n;
}

static void pc_http_send(struct mg_connection *c)
{
    mg_printf(c, "%s %s HTTP/1.1\r\nHost: %.*s\r\n",
              s_http.method, s_http.path,
              (int)s_http.host.len, s_http.host.buf);
    if (s_http.headers != NULL && s_http.headers[0] != '\0') {
        mg_printf(c, "%s\r\n", s_http.headers);
    }
    mg_printf(c, "Content-Length: %u\r\nConnection: close\r\n\r\n",
              (unsigned)s_http.body_len);
    if (s_http.body != NULL && s_http.body_len > 0) {
        mg_send(c, s_http.body, s_http.body_len);
    }
}

static void pc_http_fn(struct mg_connection *c, int ev, void *ev_data)
{
    switch (ev) {
    case MG_EV_CONNECT:
        pc_http_send(c);
        break;

    case MG_EV_HTTP_MSG: {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        iot_http_response_t *response = s_http.response;
        uint32_t cap;
        uint32_t n;

        response->status_code = (uint16_t)mg_http_status(hm);
        response->content_length = pc_http_content_length(hm);

        if (response->body != NULL && response->body_cap > 0) {
            cap = response->body_cap;
            n = (hm->body.len < cap) ? (uint32_t)hm->body.len : cap;
            memcpy(response->body, hm->body.buf, n);
            response->body_len = n;
            if (n < (uint32_t)hm->body.len) {
                s_http.overflow = true;
            }
        }

        s_http.done = true;
        c->is_closing = 1;
        break;
    }

    case MG_EV_ERROR:
        s_http.failed = true;
        s_http.done = true;
        c->is_closing = 1;
        break;

    case MG_EV_CLOSE:
        s_http.conn = NULL;
        if (!s_http.done) {
            s_http.failed = true;
            s_http.done = true;
        }
        break;

    default:
        break;
    }
}

/* ---------------- iot_http 接口实现 ---------------- */

iot_status_t iot_http_init(void)
{
    (void)pc_net_mgr();
    return IOT_OK;
}

iot_status_t iot_http_deinit(void)
{
    return IOT_OK;
}

iot_status_t iot_http_request(const iot_http_request_t *request,
                              iot_http_response_t *response)
{
    char url_buf[PC_HTTP_URL_MAX];
    const char *url;
    uint32_t timeout;
    uint64_t start;
    struct mg_connection *conn;

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

    if (strncmp(request->url, "https://", 8) == 0) {
        return IOT_NOT_SUPPORTED;      /* 未启用 TLS 后端 */
    }

    url = pc_http_url(request->url, url_buf, sizeof(url_buf));
    if (url == NULL) {
        return IOT_INVALID_PARAM;
    }

    memset(&s_http, 0, sizeof(s_http));
    s_http.method = pc_http_method(request->method);
    s_http.path = mg_url_uri(url);
    s_http.host = mg_url_host(url);
    s_http.headers = request->headers;
    s_http.body = request->body;
    s_http.body_len = (request->body != NULL) ? request->body_len : 0;
    s_http.response = response;

    conn = mg_http_connect(pc_net_mgr(), url, pc_http_fn, NULL);
    if (conn == NULL) {
        return IOT_IO_ERROR;
    }
    s_http.conn = conn;

    timeout = (request->timeout != 0) ? request->timeout
                                      : PC_HTTP_DEFAULT_TIMEOUT_MS;
    start = mg_millis();
    while (!s_http.done) {
        if (mg_millis() - start >= timeout) {
            break;
        }
        pc_net_poll(PC_HTTP_POLL_MS);
    }

    if (!s_http.done) {
        if (s_http.conn != NULL) {
            s_http.conn->is_closing = 1;
            pc_net_poll(0);
        }
        return IOT_TIMEOUT;
    }

    if (s_http.failed) {
        return IOT_IO_ERROR;
    }
    if (s_http.overflow) {
        return IOT_OVERFLOW;
    }
    return IOT_OK;
}

#endif /* IOT_NETWORK_PC */
