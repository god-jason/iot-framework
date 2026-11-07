/* HTTP/1.1 客户端测试：URL 解析与模拟传输执行。 */
#include "test.h"
#include "http.h"

#include <string.h>

typedef struct {
    uint8_t resp[1024];
    uint32_t resp_len;
    uint32_t resp_pos;

    char req[1024];
    uint32_t req_len;
} http_mock_t;

static int h_connect(void *ctx, const char *host, uint16_t port, uint32_t timeout)
{
    (void)ctx;
    (void)host;
    (void)port;
    (void)timeout;
    return 0;
}

static int h_write(void *ctx, const uint8_t *data, uint32_t len)
{
    http_mock_t *m = (http_mock_t *)ctx;
    if (m->req_len + len > sizeof(m->req) - 1) {
        return -1;
    }
    memcpy(&m->req[m->req_len], data, len);
    m->req_len += len;
    m->req[m->req_len] = '\0';
    return (int)len;
}

static int h_read(void *ctx, uint8_t *data, uint32_t len, uint32_t timeout)
{
    http_mock_t *m = (http_mock_t *)ctx;
    uint32_t avail = m->resp_len - m->resp_pos;
    uint32_t n;
    (void)timeout;

    if (avail == 0) {
        return 0;
    }
    n = (len < avail) ? len : avail;
    memcpy(data, &m->resp[m->resp_pos], n);
    m->resp_pos += n;
    return (int)n;
}

static void h_close(void *ctx)
{
    (void)ctx;
}

static http_transport_t make_transport(http_mock_t *mock)
{
    http_transport_t t;
    t.connect = h_connect;
    t.write = h_write;
    t.read = h_read;
    t.close = h_close;
    t.ctx = mock;
    return t;
}

static void mock_set_response(http_mock_t *m, const char *text)
{
    m->resp_len = (uint32_t)strlen(text);
    memcpy(m->resp, text, m->resp_len);
    m->resp_pos = 0;
}

static void test_parse_url(void)
{
    char host[128];
    char path[512];
    uint16_t port = 0;

    tf_begin("http/URL 解析");
    TF_CHECK_INT(http_parse_url("http://example.com/api?x=1", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_STR(host, "example.com");
    TF_CHECK_INT(port, 80);
    TF_CHECK_STR(path, "/api?x=1");

    TF_CHECK_INT(http_parse_url("https://host:8443/p", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_STR(host, "host");
    TF_CHECK_INT(port, 8443);
    TF_CHECK_STR(path, "/p");

    TF_CHECK_INT(http_parse_url("http://example.com", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_STR(host, "example.com");
    TF_CHECK_STR(path, "/");

    TF_CHECK_INT(http_parse_url("https://example.com", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_INT(port, 443);

    TF_CHECK_INT(http_parse_url("http://example.com:8080", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_INT(port, 8080);
    TF_CHECK_STR(path, "/");
    tf_end();

    tf_begin("http/URL 非法输入");
    TF_CHECK_INT(http_parse_url("", host, sizeof(host), &port,
                                path, sizeof(path)), -1);
    TF_CHECK_INT(http_parse_url("http://", host, sizeof(host), &port,
                                path, sizeof(path)), -1);
    TF_CHECK_INT(http_parse_url(NULL, host, sizeof(host), &port,
                                path, sizeof(path)), -1);
    tf_end();
}

static void test_execute(void)
{
    http_mock_t mock;
    http_transport_t transport;
    http_request_t req;
    http_response_t resp;

    tf_begin("http/执行 POST 并解析响应");
    memset(&mock, 0, sizeof(mock));
    mock_set_response(&mock,
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
    transport = make_transport(&mock);

    memset(&req, 0, sizeof(req));
    req.url = "http://test.local/x";
    req.method = "POST";
    req.headers = "X-Test: 1";
    req.body = (const uint8_t *)"abc";
    req.body_len = 3;
    req.timeout = 1000;

    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK_INT(resp.status_code, 200);
    TF_CHECK_INT(resp.content_length, 5);
    TF_CHECK_INT(resp.body_len, 5);
    TF_CHECK_MEM(resp.body, "hello", 5);

    TF_CHECK(strstr(mock.req, "POST /x HTTP/1.1\r\n") != NULL);
    TF_CHECK(strstr(mock.req, "Host: test.local\r\n") != NULL);
    TF_CHECK(strstr(mock.req, "X-Test: 1\r\n") != NULL);
    TF_CHECK(strstr(mock.req, "Content-Length: 3\r\n") != NULL);
    TF_CHECK(strstr(mock.req, "\r\n\r\nabc") != NULL);
    tf_end();

    tf_begin("http/无 Content-Length 的响应");
    memset(&mock, 0, sizeof(mock));
    mock_set_response(&mock, "HTTP/1.1 404 Not Found\r\n\r\nnope");
    transport = make_transport(&mock);

    memset(&req, 0, sizeof(req));
    req.url = "http://test.local/missing";
    req.method = "GET";
    req.timeout = 1000;

    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK_INT(resp.status_code, 404);
    TF_CHECK_INT(resp.body_len, 4);
    TF_CHECK_MEM(resp.body, "nope", 4);
    TF_CHECK(strstr(mock.req, "GET /missing HTTP/1.1\r\n") != NULL);
    TF_CHECK(strstr(mock.req, "Content-Length: 0\r\n") != NULL);
    tf_end();

    tf_begin("http/参数校验");
    {
        http_mock_t empty;
        http_transport_t t;
        memset(&empty, 0, sizeof(empty));
        t = make_transport(&empty);
        TF_CHECK_INT(http_execute(NULL, &req, &resp), -1);
        TF_CHECK_INT(http_execute(&t, NULL, &resp), -1);
        TF_CHECK_INT(http_execute(&t, &req, NULL), -1);
    }
    tf_end();
}

void test_http(void)
{
    test_parse_url();
    test_execute();
}
