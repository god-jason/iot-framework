/*
 * ml307_http.c - ML307 原生 HTTP 实现（AT+HTTP*），平替 components/http
 *
 * 启用方式：ML307 目标的构建系统编译本文件，并排除 components/http/http_client.c。
 * 使用模组内置 HTTP 协议栈，不经过 net socket，节省 RAM 与交互次数。
 *
 * 指令序列（若模组固件不同，只需调整本文件）：
 *   AT+HTTPINIT
 *   AT+HTTPPARA="URL","http://host[:port]/path"
 *   AT+HTTPPARA="CONTENT","<content-type>"        (POST)
 *   AT+HTTPDATA=<len>,<timeout_ms>  →  '>'  → <body> → OK
 *   AT+HTTPACTION=0|1                             (GET|POST)
 *   +HTTPACTION: <method>,<status>,<datalen>
 *   AT+HTTPREAD                                   → +HTTPREAD: <len> + body
 *   AT+HTTPTERM
 *
 * 限制：不支持 HTTPS、任意自定义请求头（返回 NET_ERR_NOT_SUPPORTED）；
 *       响应体一次读出，受 AT 行缓冲大小限制。
 */

#include "http.h"
#include "ml307.h"
#include "at.h"
#include "at_urc.h"
#include "hal_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ML307_HTTP_URC          "+HTTPACTION:"
#define ML307_HTTP_DEFAULT_MS   30000U
#define ML307_HTTP_URL_MAX      320U
#define ML307_HTTP_READ_TIMEOUT 15000U

static bool s_http_urc;
static volatile bool s_action_done;
static volatile int s_action_status;
static volatile int s_action_len;

static void ml307_http_on_urc(const char *line, uint32_t len, void *user)
{
    int method = 0;
    int status = 0;
    int dlen = 0;

    (void)user;
    if (line == NULL || len == 0U) {
        return;
    }
    if (sscanf(line, "+HTTPACTION: %d,%d,%d", &method, &status, &dlen) >= 2) {
        s_action_status = status;
        s_action_len = (dlen > 0) ? dlen : 0;
        s_action_done = true;
    }
}

static int ml307_http_ensure_urc(void)
{
    if (!s_http_urc) {
        if (at_urc_register(ML307_HTTP_URC, ml307_http_on_urc, NULL) != 0) {
            return NET_ERR_NOMEM;
        }
        s_http_urc = true;
    }
    return NET_OK;
}

static int ml307_http_build_url(const http_url_t *u, char *buf, uint32_t size)
{
    int n;

    if (u->port == 80U) {
        n = snprintf(buf, size, "http://%s%s", u->host, u->path);
    } else {
        n = snprintf(buf, size, "http://%s:%u%s", u->host,
                     (unsigned)u->port, u->path);
    }
    return (n > 0 && n < (int)size) ? NET_OK : NET_ERR_PARAM;
}

/* 发送 POST 数据（HTTPDATA 阶段） */
static int ml307_http_send_body(const void *body, uint32_t len)
{
    char cmd[48];
    at_request_t phase;

    snprintf(cmd, sizeof(cmd), "AT+HTTPDATA=%u,10000", (unsigned)len);
    memset(&phase, 0, sizeof(phase));
    phase.command = cmd;
    phase.success = ">";
    phase.failure = "ERROR";
    phase.timeout = 10000U;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    at_send_raw((const uint8_t *)body, len);

    memset(&phase, 0, sizeof(phase));
    phase.command = NULL;
    phase.success = "OK";
    phase.failure = "ERROR";
    phase.timeout = 10000U;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }
    return NET_OK;
}

static int ml307_http_request(const char *method, const char *url,
                              const http_header_t *headers,
                              uint32_t header_count,
                              const char *content_type, const void *body,
                              uint32_t body_len, http_response_t *rsp,
                              http_body_cb cb, void *user,
                              uint32_t timeout_ms)
{
    http_url_t u;
    char urlbuf[ML307_HTTP_URL_MAX];
    char cmd[ML307_HTTP_URL_MAX + 48U];
    uint32_t wait = (timeout_ms != 0U) ? timeout_ms : ML307_HTTP_DEFAULT_MS;
    uint32_t start;
    bool is_post;
    int rc;

    if (method == NULL || http_url_parse(url, &u) != NET_OK) {
        return NET_ERR_PARAM;
    }
    if (u.https) {
        return NET_ERR_NOT_SUPPORTED;
    }
    /* 模组 HTTP 协议栈不支持任意请求头 */
    if (headers != NULL && header_count != 0U) {
        return NET_ERR_NOT_SUPPORTED;
    }
    is_post = (strcmp(method, "POST") == 0);
    if (body == NULL && body_len != 0U) {
        return NET_ERR_PARAM;
    }
    if (is_post && body_len != 0U && body == NULL) {
        return NET_ERR_PARAM;
    }
    if (rsp != NULL) {
        memset(rsp, 0, sizeof(*rsp));
    }
    rc = ml307_http_ensure_urc();
    if (rc != NET_OK) {
        return rc;
    }
    rc = ml307_http_build_url(&u, urlbuf, sizeof(urlbuf));
    if (rc != NET_OK) {
        return rc;
    }

    if (at_exec("AT+HTTPINIT", 5000U) != AT_RESULT_OK) {
        return NET_ERR_IO;
    }

    snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", urlbuf);
    if (at_exec(cmd, 5000U) != AT_RESULT_OK) {
        goto fail;
    }
    if (is_post) {
        snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"CONTENT\",\"%s\"",
                 (content_type != NULL) ? content_type
                                        : "application/octet-stream");
        if (at_exec(cmd, 5000U) != AT_RESULT_OK) {
            goto fail;
        }
        if (body_len != 0U && ml307_http_send_body(body, body_len) != NET_OK) {
            goto fail;
        }
    }

    s_action_done = false;
    s_action_status = 0;
    s_action_len = 0;
    snprintf(cmd, sizeof(cmd), "AT+HTTPACTION=%d", is_post ? 1 : 0);
    if (at_exec(cmd, 5000U) != AT_RESULT_OK) {
        goto fail;
    }

    start = hal_tick_get();
    while (!s_action_done && (uint32_t)(hal_tick_get() - start) < wait) {
        (void)at_process();
        hal_delay_ms(10U);
    }
    if (!s_action_done) {
        goto fail;
    }
    if (rsp != NULL) {
        rsp->status = s_action_status;
        rsp->content_length = (uint32_t)s_action_len;
    }

    if (cb != NULL && s_action_len > 0) {
        const char *resp;
        const char *p;
        const char *body_start;
        uint32_t avail;
        uint32_t copy;

        if (at_exec("AT+HTTPREAD", ML307_HTTP_READ_TIMEOUT) != AT_RESULT_OK) {
            goto fail;
        }
        resp = at_get_response();
        p = (resp != NULL) ? strstr(resp, "+HTTPREAD:") : NULL;
        if (p == NULL) {
            goto fail;
        }
        p = strchr(p, '\n');
        if (p == NULL) {
            goto fail;
        }
        body_start = p + 1;
        avail = (uint32_t)strlen(body_start);
        copy = ((uint32_t)s_action_len < avail) ? (uint32_t)s_action_len
                                                : avail;
        if (copy > 0U && cb((const uint8_t *)body_start, copy, user) != 0) {
            goto fail;
        }
    }

    (void)at_exec("AT+HTTPTERM", 3000U);
    return NET_OK;

fail:
    (void)at_exec("AT+HTTPTERM", 3000U);
    return NET_ERR_IO;
}

int http_get(const char *url, const http_header_t *headers,
             uint32_t header_count, http_response_t *rsp, http_body_cb cb,
             void *user, uint32_t timeout_ms)
{
    return ml307_http_request("GET", url, headers, header_count, NULL, NULL,
                              0U, rsp, cb, user, timeout_ms);
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
    return ml307_http_request("POST", url, headers, header_count,
                              content_type, body, body_len, rsp, cb, user,
                              timeout_ms);
}

typedef struct {
    FILE *fp;
} ml307_http_file_t;

static int ml307_http_file_write(const uint8_t *data, uint32_t len, void *user)
{
    ml307_http_file_t *f = (ml307_http_file_t *)user;

    if (f == NULL || f->fp == NULL) {
        return -1;
    }
    return (fwrite(data, 1U, len, f->fp) == len) ? 0 : -1;
}

int http_download(const char *url, const http_header_t *headers,
                  uint32_t header_count, const char *path,
                  uint32_t timeout_ms)
{
    ml307_http_file_t f;
    http_response_t rsp;
    int rc;

    if (path == NULL) {
        return NET_ERR_PARAM;
    }
    f.fp = fopen(path, "wb");
    if (f.fp == NULL) {
        return NET_ERR_IO;
    }
    rc = http_get(url, headers, header_count, &rsp, ml307_http_file_write,
                  &f, timeout_ms);
    (void)fclose(f.fp);
    if (rc != NET_OK || rsp.status < 200 || rsp.status >= 300) {
        (void)remove(path);
        return (rc != NET_OK) ? rc : NET_ERR_IO;
    }
    return NET_OK;
}
