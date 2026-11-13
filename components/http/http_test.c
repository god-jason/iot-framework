/*
 * http_test.c - http 组件回归测试（本地回环 HTTP 服务）
 */

#include "test.h"
#include "http.h"
#include "net_test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if NET_ENABLE

#define HTTP_TEST_PORT  18082U

static char g_body[512];
static uint32_t g_body_len;

static int body_cb(const uint8_t *data, uint32_t len, void *user)
{
    (void)user;
    if (g_body_len + len >= sizeof(g_body)) {
        len = (uint32_t)sizeof(g_body) - g_body_len - 1U;
    }
    memcpy(&g_body[g_body_len], data, len);
    g_body_len += len;
    g_body[g_body_len] = '\0';
    return 0;
}

static void http_server(intptr_t fd, void *user)
{
    char req[1024];
    char body[256];
    char resp[384];
    uint32_t n = 0U;
    uint32_t clen = 0U;
    uint32_t got = 0U;
    int body_len;
    bool is_post;

    (void)user;
    while (n + 1U < sizeof(req)) {
        if (ntu_recv_some(fd, &req[n], 1U) != 1) {
            return;
        }
        n++;
        if (n >= 4U && memcmp(&req[n - 4U], "\r\n\r\n", 4U) == 0) {
            break;
        }
    }
    req[n] = '\0';

    {
        const char *p = strstr(req, "Content-Length:");

        if (p != NULL) {
            clen = (uint32_t)strtoul(p + 15, NULL, 10);
        }
    }
    if (clen >= sizeof(body)) {
        clen = (uint32_t)sizeof(body) - 1U;
    }
    while (got < clen) {
        int c = ntu_recv_some(fd, &body[got], clen - got);

        if (c <= 0) {
            break;
        }
        got += (uint32_t)c;
    }
    body[got] = '\0';
    is_post = (strncmp(req, "POST ", 5U) == 0);

    if (!is_post && strstr(req, " /hello ") != NULL) {
        body_len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 5\r\n"
            "Connection: close\r\n\r\nhello");
    } else if (is_post && strstr(req, " /echo ") != NULL) {
        body_len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %u\r\n"
            "Connection: close\r\n\r\n",
            (unsigned)got);
        if (body_len > 0 && (uint32_t)body_len + got < sizeof(resp)) {
            memcpy(&resp[body_len], body, got);
            body_len += (int)got;
        }
    } else if (!is_post && strstr(req, " /hdr ") != NULL) {
        bool has = (strstr(req, "X-Test: 42") != NULL);

        body_len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n\r\n%s",
            has ? "ok" : "no");
    } else {
        body_len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 4\r\n"
            "Connection: close\r\n\r\nnope");
    }
    if (body_len > 0) {
        (void)ntu_send_all(fd, resp, (uint32_t)body_len);
    }
}

static void body_reset(void)
{
    g_body_len = 0U;
    g_body[0] = '\0';
}

void ut_http(void)
{
    ntu_server_t srv;
    http_url_t u;
    http_response_t rsp;
    int rc;

    tf_begin("http/GET POST 下载");

    TF_CHECK_INT(net_init(), NET_OK);

    /* URL 解析 */
    TF_CHECK_INT(http_url_parse("http://example.com/a/b?c=1", &u), NET_OK);
    TF_CHECK(strcmp(u.host, "example.com") == 0);
    TF_CHECK_INT((int)u.port, 80);
    TF_CHECK(strcmp(u.path, "/a/b?c=1") == 0);
    TF_CHECK(!u.https);
    TF_CHECK_INT(http_url_parse("http://1.2.3.4:8080", &u), NET_OK);
    TF_CHECK_INT((int)u.port, 8080);
    TF_CHECK(strcmp(u.path, "/") == 0);
    TF_CHECK_INT(http_url_parse("ftp://x/", &u), NET_ERR_PARAM);
    TF_CHECK_INT(http_url_parse("https://x/", &u), NET_OK);
    TF_CHECK(u.https);

    TF_CHECK(ntu_server_start(&srv, HTTP_TEST_PORT, http_server, NULL));

    /* GET */
    body_reset();
    rc = http_get("http://127.0.0.1:18082/hello", NULL, 0U, &rsp, body_cb,
                  NULL, 3000U);
    TF_CHECK_INT(rc, NET_OK);
    TF_CHECK_INT(rsp.status, 200);
    TF_CHECK_INT((int)rsp.content_length, 5);
    TF_CHECK(strcmp(rsp.content_type, "text/plain") == 0);
    TF_CHECK_INT((int)g_body_len, 5);
    TF_CHECK(memcmp(g_body, "hello", 5U) == 0);

    /* 自定义请求头 */
    {
        static const http_header_t hdrs[] = { { "X-Test", "42" } };

        body_reset();
        rc = http_get("http://127.0.0.1:18082/hdr", hdrs, 1U, &rsp, body_cb,
                      NULL, 3000U);
        TF_CHECK_INT(rc, NET_OK);
        TF_CHECK_INT(rsp.status, 200);
        TF_CHECK(strcmp(g_body, "ok") == 0);
    }

    /* 非法请求头：直接参数错误 */
    {
        static const http_header_t bad[] = { { "Bad:Name", "1" } };

        TF_CHECK_INT(http_get("http://127.0.0.1:18082/hdr", bad, 1U, NULL,
                              NULL, NULL, 1000U), NET_ERR_PARAM);
    }

    /* POST */
    body_reset();
    rc = http_post("http://127.0.0.1:18082/echo", NULL, 0U, "text/plain",
                   "ping123", 7U, &rsp, body_cb, NULL, 3000U);
    TF_CHECK_INT(rc, NET_OK);
    TF_CHECK_INT(rsp.status, 200);
    TF_CHECK_INT((int)g_body_len, 7);
    TF_CHECK(memcmp(g_body, "ping123", 7U) == 0);

    /* 文件下载 */
    TF_CHECK_INT(http_download("http://127.0.0.1:18082/hello", NULL, 0U,
                               "net_http_dl.tmp", 3000U), NET_OK);
    {
        FILE *f = fopen("net_http_dl.tmp", "rb");
        char b[8] = { 0 };
        size_t r = 0U;

        if (f != NULL) {
            r = fread(b, 1U, 5U, f);
            (void)fclose(f);
        }
        TF_CHECK_INT((int)r, 5);
        TF_CHECK(b[0] == 'h' && b[4] == 'o');
        (void)remove("net_http_dl.tmp");
    }

    /* 404 原样返回状态码 */
    body_reset();
    rc = http_get("http://127.0.0.1:18082/missing", NULL, 0U, &rsp, body_cb,
                  NULL, 3000U);
    TF_CHECK_INT(rc, NET_OK);
    TF_CHECK_INT(rsp.status, 404);

    /* HTTPS 不支持 */
    TF_CHECK_INT(http_get("https://127.0.0.1:18082/hello", NULL, 0U,
                          NULL, NULL, NULL, 1000U), NET_ERR_NOT_SUPPORTED);
    TF_CHECK_INT(http_get("bad-url", NULL, 0U, NULL, NULL, NULL, 1000U),
                 NET_ERR_PARAM);

    ntu_server_stop(&srv);
    TF_CHECK_INT(net_deinit(), NET_OK);
    tf_end();
}

#else

void ut_http(void)
{
    tf_begin("http/GET POST 下载");
    tf_end();
}

#endif /* NET_ENABLE */
