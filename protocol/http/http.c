/*
 * 通用 HTTP/1.1 客户端实现。
 * 依赖 http_transport_t 抽象底层连接；响应体写入内部静态缓冲。
 */
#include "http.h"

#include <string.h>
#include <stdio.h>

#define HTTP_RESP_BUF 4096

static uint8_t s_body[HTTP_RESP_BUF];

/* 大小写不敏感前缀匹配 */
static int starts_ci(const char *s, const char *prefix, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        char a = s[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static int parse_int(const char *s)
{
    int v = 0;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

int http_parse_url(const char *url, char *host, uint32_t host_size,
                   uint16_t *port, char *path, uint32_t path_size)
{
    const char *p;
    const char *host_start, *host_end, *path_start;
    uint16_t pval = 80;
    uint32_t len;

    if (url == NULL || host == NULL || path == NULL) {
        return -1;
    }
    p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        pval = 80;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        pval = 443;
    }

    host_start = p;
    host_end = p;
    while (*host_end != '\0' && *host_end != '/' && *host_end != ':') {
        host_end++;
    }
    if (host_start == host_end) {
        return -1;
    }

    path_start = host_end;
    if (*host_end == ':') {
        const char *q = host_end + 1;
        uint32_t pnum = 0;
        while (*q >= '0' && *q <= '9') {
            pnum = pnum * 10u + (uint32_t)(*q - '0');
            q++;
        }
        if (pnum > 0 && pnum <= 65535) {
            pval = (uint16_t)pnum;
        }
        while (*q != '\0' && *q != '/') {
            q++;
        }
        path_start = q;
    }

    len = (uint32_t)(host_end - host_start);
    if (len >= host_size) {
        len = host_size - 1;
    }
    memcpy(host, host_start, len);
    host[len] = '\0';

    if (port != NULL) {
        *port = pval;
    }

    if (*path_start == '\0') {
        if (path_size < 2) {
            return -1;
        }
        path[0] = '/';
        path[1] = '\0';
    } else {
        len = (uint32_t)strlen(path_start);
        if (len >= path_size) {
            len = path_size - 1;
        }
        memcpy(path, path_start, len);
        path[len] = '\0';
    }
    return IOT_OK;
}

/* 在缓冲中查找 "\r\n\r\n"，返回其结束位置，未找到返回 -1 */
static int find_header_end(const uint8_t *p, uint32_t len)
{
    uint32_t i;
    for (i = 0; i + 3 < len; i++) {
        if (p[i] == '\r' && p[i + 1] == '\n' &&
            p[i + 2] == '\r' && p[i + 3] == '\n') {
            return (int)(i + 4);
        }
    }
    return -1;
}

/* 从头部区域解析 Content-Length；未找到返回 -1 */
static int parse_content_length(const uint8_t *p, uint32_t header_len)
{
    uint32_t i;
    for (i = 0; i < header_len; i++) {
        const char *line = (const char *)&p[i];
        if (starts_ci(line, "Content-Length:", 15)) {
            return parse_int(line + 15);
        }
        /* 跳到下一行 */
        while (i < header_len && p[i] != '\n') {
            i++;
        }
    }
    return -1;
}

int http_execute(const http_transport_t *transport,
                 const http_request_t *request,
                 http_response_t *response)
{
    char host[128];
    char path[512];
    uint16_t port;
    char head[1024];
    uint32_t w = 0;
    const char *method;
    uint32_t used = 0;
    int header_end;
    int content_length;

    if (transport == NULL || request == NULL || response == NULL ||
        transport->connect == NULL || transport->write == NULL ||
        transport->read == NULL) {
        return -1;
    }
    memset(response, 0, sizeof(*response));

    if (http_parse_url(request->url, host, sizeof(host), &port,
                       path, sizeof(path)) != IOT_OK) {
        return -1;
    }

    if (transport->connect(transport->ctx, host, port, request->timeout) != 0) {
        return -1;
    }

    /* ---- 构建请求头部 ---- */
    method = (request->method != NULL && request->method[0] != '\0')
             ? request->method : "GET";

    w = 0;

    /* ---- 精确构造请求头 ---- */
    {
        uint32_t cap = sizeof(head);
        uint32_t off = 0;
        off += (uint32_t)snprintf(head + off, cap - off,
                                  "%s %s HTTP/1.1\r\nHost: %s\r\n",
                                  method, path, host);
        if (request->headers != NULL && request->headers[0] != '\0') {
            off += (uint32_t)snprintf(head + off, cap - off, "%s\r\n",
                                      request->headers);
        }
        off += (uint32_t)snprintf(head + off, cap - off,
                                  "Content-Length: %u\r\nConnection: close\r\n\r\n",
                                  (unsigned)request->body_len);
        w = off;
    }

    if (transport->write(transport->ctx, (const uint8_t *)head, w) != (int)w) {
        if (transport->close != NULL) {
            transport->close(transport->ctx);
        }
        return -1;
    }
    if (request->body != NULL && request->body_len > 0) {
        if (transport->write(transport->ctx, request->body, request->body_len) !=
            (int)request->body_len) {
            if (transport->close != NULL) {
                transport->close(transport->ctx);
            }
            return -1;
        }
    }

    /* ---- 读取响应头 ---- */
    header_end = -1;
    while (used < HTTP_RESP_BUF) {
        uint32_t space = HTTP_RESP_BUF - used;
        int n = transport->read(transport->ctx, &s_body[used], space,
                                request->timeout);
        if (n <= 0) {
            break;   /* 关闭 / 超时 / 错误 */
        }
        used += (uint32_t)n;
        header_end = find_header_end(s_body, used);
        if (header_end >= 0) {
            break;
        }
    }

    if (header_end < 0) {
        if (transport->close != NULL) {
            transport->close(transport->ctx);
        }
        return -1;
    }

    response->status_code = 0;
    if (memcmp(s_body, "HTTP/1.", 7) == 0 && used >= 12) {
        response->status_code = (uint16_t)parse_int((const char *)&s_body[9]);
    }

    content_length = parse_content_length(s_body, (uint32_t)header_end);
    if (content_length > 0) {
        response->content_length = (uint32_t)content_length;
    }

    /* ---- 依据 Content-Length 补齐响应体 ---- */
    if (content_length > 0) {
        while ((int)(used - (uint32_t)header_end) < content_length &&
               used < HTTP_RESP_BUF) {
            uint32_t space = HTTP_RESP_BUF - used;
            int n = transport->read(transport->ctx, &s_body[used], space,
                                    request->timeout);
            if (n <= 0) {
                break;
            }
            used += (uint32_t)n;
        }
    }

    if (transport->close != NULL) {
        transport->close(transport->ctx);
    }

    {
        uint32_t have = used - (uint32_t)header_end;
        if (content_length > 0 && have > (uint32_t)content_length) {
            have = (uint32_t)content_length;
        }
        response->body = &s_body[header_end];
        response->body_len = have;
    }
    return IOT_OK;
}