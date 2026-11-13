/*
 * http_client.c - 通用 HTTP/1.1 客户端（GET / POST / 文件下载）
 *
 * 运行在 net 的流式接口之上；ML307 等模组目标改编译模组原生实现
 * （modules/ml307/ml307_http.c），本文件由构建系统排除。
 *
 * 请求统一携带 Connection: close，响应体读到对端关闭为止；
 * 头部逐字节读取以避免把响应体提前吞掉，读到空行后再流式回传 body。
 * 自定义请求头通过 http_header_t 数组传入，名称/值会做 CR/LF 注入校验。
 */

#include "http.h"
#include "ros.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>

#define HTTP_HDR_MAX     1024U
#define HTTP_IO_CHUNK    512U
#define HTTP_LINE_MAX    256U

/* 请求头合法性：名称非空且不含 ':'，名称/值不含 CR/LF */

static bool http_header_valid(const http_header_t *h)
{
    const char *p;

    if (h == NULL || h->name == NULL || h->name[0] == '\0' ||
        h->value == NULL) {
        return false;
    }
    for (p = h->name; *p != '\0'; p++) {
        if (*p == '\r' || *p == '\n' || *p == ':') {
            return false;
        }
    }
    for (p = h->value; *p != '\0'; p++) {
        if (*p == '\r' || *p == '\n') {
            return false;
        }
    }
    return true;
}

/* 从头部文本中取 Content-Length / Content-Type / 状态码 */
static void http_parse_headers(const char *hdr, http_response_t *rsp)
{
    const char *p = hdr;

    if (rsp == NULL) {
        return;
    }
    if (str_starts_with(p, "HTTP/")) {
        const char *sp = strchr(p, ' ');

        if (sp != NULL) {
            rsp->status = (int)strtol(sp + 1, NULL, 10);
        }
    }
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (str_nicmp(p, "Content-Length:", 15U)) {
            rsp->content_length = (uint32_t)strtoul(p + 15, NULL, 10);
        } else if (str_nicmp(p, "Content-Type:", 13U)) {
            const char *v = str_skip_ws(p + 13);
            const char *eol;
            size_t n;

            eol = strstr(v, "\r\n");
            n = (eol != NULL) ? (size_t)(eol - v) : strlen(v);
            if (n >= sizeof(rsp->content_type)) {
                n = sizeof(rsp->content_type) - 1U;
            }
            memcpy(rsp->content_type, v, n);
            rsp->content_type[n] = '\0';
        }
    }
}

static int http_request(const char *method, const char *url,
                        const http_header_t *headers, uint32_t header_count,
                        const char *content_type, const void *body,
                        uint32_t body_len, http_response_t *rsp,
                        http_body_cb cb, void *user, uint32_t timeout_ms)
{
    http_url_t u;
    net_sock_t sock;
    net_stream_t stream;
    char line[HTTP_LINE_MAX];
    char header[HTTP_HDR_MAX];
    uint32_t hlen = 0U;
    uint32_t i;
    uint8_t chunk[HTTP_IO_CHUNK];
    int n;
    int rc;

    if (method == NULL || http_url_parse(url, &u) != NET_OK) {
        return NET_ERR_PARAM;
    }
    if (u.https) {
        return NET_ERR_NOT_SUPPORTED;
    }
    if (headers == NULL && header_count != 0U) {
        return NET_ERR_PARAM;
    }
    for (i = 0U; i < header_count; i++) {
        if (!http_header_valid(&headers[i])) {
            return NET_ERR_PARAM;
        }
    }
    if (rsp != NULL) {
        memset(rsp, 0, sizeof(*rsp));
    }

    rc = net_open(&sock, NET_TCP, u.host, u.port, timeout_ms);
    if (rc != NET_OK) {
        return rc;
    }
    rc = net_stream_init(&stream, &sock, timeout_ms);
    if (rc != NET_OK) {
        (void)net_close(&sock);
        return rc;
    }

    n = snprintf(line, sizeof(line),
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: robotos-net\r\n"
                 "Accept: */*\r\n"
                 "Connection: close\r\n",
                 method, u.path, u.host);
    if (n <= 0 || n >= (int)sizeof(line)) {
        rc = NET_ERR_PARAM;
        goto close_stream;
    }
    rc = net_stream_write(&stream, line, (uint32_t)n);
    if (rc < 0) {
        goto close_stream;
    }

    /* 自定义请求头 */
    for (i = 0U; i < header_count; i++) {
        n = snprintf(line, sizeof(line), "%s: %s\r\n",
                     headers[i].name, headers[i].value);
        if (n <= 0 || n >= (int)sizeof(line)) {
            rc = NET_ERR_PARAM;
            goto close_stream;
        }
        rc = net_stream_write(&stream, line, (uint32_t)n);
        if (rc < 0) {
            goto close_stream;
        }
    }

    if (body != NULL && body_len > 0U) {
        n = snprintf(line, sizeof(line),
                     "Content-Type: %s\r\nContent-Length: %u\r\n",
                     (content_type != NULL) ? content_type
                                            : "application/octet-stream",
                     (unsigned)body_len);
        if (n <= 0 || n >= (int)sizeof(line)) {
            rc = NET_ERR_PARAM;
            goto close_stream;
        }
        rc = net_stream_write(&stream, line, (uint32_t)n);
        if (rc < 0) {
            goto close_stream;
        }
    }

    rc = net_stream_write(&stream, "\r\n", 2U);
    if (rc < 0) {
        goto close_stream;
    }
    if (body != NULL && body_len > 0U) {
        rc = net_stream_write(&stream, body, body_len);
        if (rc < 0) {
            goto close_stream;
        }
    }
    rc = net_stream_flush(&stream);
    if (rc < 0) {
        goto close_stream;
    }

    /* 逐字节读头部，直到空行 */
    while (hlen < sizeof(header) - 1U) {
        char c;

        rc = net_stream_read(&stream, &c, 1U);
        if (rc <= 0) {
            rc = NET_ERR_IO;
            goto close_stream;
        }
        header[hlen++] = c;
        if (hlen >= 4U && memcmp(&header[hlen - 4U], "\r\n\r\n", 4U) == 0) {
            break;
        }
    }
    header[hlen] = '\0';
    http_parse_headers(header, rsp);

    /* 流式读取响应体 */
    for (;;) {
        rc = net_stream_read(&stream, chunk, sizeof(chunk));
        if (rc == NET_ERR_CLOSED) {
            break;
        }
        if (rc < 0) {
            goto close_stream;
        }
        if (rc == 0) {
            break;              /* 超时：结束本次读取 */
        }
        if (cb != NULL && cb(chunk, (uint32_t)rc, user) != 0) {
            rc = NET_ERR_IO;
            goto close_stream;
        }
    }
    rc = NET_OK;

close_stream:
    (void)net_stream_close(&stream);
    return rc;
}

int http_get(const char *url, const http_header_t *headers,
             uint32_t header_count, http_response_t *rsp, http_body_cb cb,
             void *user, uint32_t timeout_ms)
{
    return http_request("GET", url, headers, header_count, NULL, NULL, 0U,
                        rsp, cb, user, timeout_ms);
}

int http_post(const char *url, const http_header_t *headers,
              uint32_t header_count, const char *content_type,
              const void *body, uint32_t body_len,
              http_response_t *rsp, http_body_cb cb, void *user,
              uint32_t timeout_ms)
{
    if (body == NULL && body_len != 0U) {
        return NET_ERR_PARAM;
    }
    return http_request("POST", url, headers, header_count, content_type,
                        body, body_len, rsp, cb, user, timeout_ms);
}

typedef struct {
    FILE *fp;
} http_file_t;

static int http_file_write(const uint8_t *data, uint32_t len, void *user)
{
    http_file_t *f = (http_file_t *)user;

    if (f == NULL || f->fp == NULL) {
        return -1;
    }
    return (fwrite(data, 1U, len, f->fp) == len) ? 0 : -1;
}

int http_download(const char *url, const http_header_t *headers,
                  uint32_t header_count, const char *path,
                  uint32_t timeout_ms)
{
    http_file_t f;
    http_response_t rsp;
    int rc;

    if (path == NULL) {
        return NET_ERR_PARAM;
    }
    f.fp = fopen(path, "wb");
    if (f.fp == NULL) {
        return NET_ERR_IO;
    }
    rc = http_get(url, headers, header_count, &rsp, http_file_write, &f,
                  timeout_ms);
    (void)fclose(f.fp);
    if (rc != NET_OK || rsp.status < 200 || rsp.status >= 300) {
        (void)remove(path);
        return (rc != NET_OK) ? rc : NET_ERR_IO;
    }
    return NET_OK;
}


