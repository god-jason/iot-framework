/* components/modem/ml307/http.c per-file 单元测试。
 *
 * 覆盖：URL 解析（scheme/端口/路径/截断/非法输入）、请求头精确构造、
 *       响应状态码与 Content-Length 解析、分片读取、响应体截断/补齐、
 *       传输层回调缺失、连接失败、写失败、头部不完整等错误路径。
 * 未覆盖：分块传输编码（chunked）、HTTPS 加解密（由传输后端负责）。
 */
#include "test.h"
#include "http.h"

#include <string.h>

#define MOCK_RESP_MAX 8192
#define MOCK_REQ_MAX 4096

typedef struct {
    char resp[MOCK_RESP_MAX];
    uint32_t resp_len;
    uint32_t resp_pos;
    uint32_t max_chunk;          /* 0 = 不限 */

    char req[MOCK_REQ_MAX];
    uint32_t req_len;

    char host[128];
    uint16_t port;
    uint32_t timeout;

    int connect_calls;
    int close_calls;
    int write_calls;
    int read_calls;
    int fail_connect;
    int fail_write;
} http_mock_t;

static int h_connect(void *ctx, const char *host, uint16_t port, uint32_t timeout)
{
    http_mock_t *m = (http_mock_t *)ctx;

    m->connect_calls++;
    m->port = port;
    m->timeout = timeout;
    if (host != NULL) {
        strncpy(m->host, host, sizeof(m->host) - 1);
        m->host[sizeof(m->host) - 1] = '\0';
    }
    return m->fail_connect ? -1 : 0;
}

static int h_write(void *ctx, const uint8_t *data, uint32_t len)
{
    http_mock_t *m = (http_mock_t *)ctx;

    m->write_calls++;
    if (m->fail_write) {
        return -1;
    }
    if (m->req_len + len >= sizeof(m->req)) {
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
    uint32_t avail;
    uint32_t n;

    (void)timeout;
    m->read_calls++;
    avail = m->resp_len - m->resp_pos;
    if (avail == 0) {
        return 0;
    }
    n = (len < avail) ? len : avail;
    if (m->max_chunk > 0 && n > m->max_chunk) {
        n = m->max_chunk;
    }
    memcpy(data, &m->resp[m->resp_pos], n);
    m->resp_pos += n;
    return (int)n;
}

static void h_close(void *ctx)
{
    http_mock_t *m = (http_mock_t *)ctx;

    m->close_calls++;
}

static http_transport_t make_transport(http_mock_t *m)
{
    http_transport_t t;

    t.connect = h_connect;
    t.write = h_write;
    t.read = h_read;
    t.close = h_close;
    t.ctx = m;
    return t;
}

static void mock_reset(http_mock_t *m)
{
    memset(m, 0, sizeof(*m));
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

    tf_begin("http/URL 基本解析");
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
    TF_CHECK_STR(path, "/");
    TF_CHECK_INT(port, 80);

    TF_CHECK_INT(http_parse_url("https://example.com", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_INT(port, 443);
    tf_end();

    tf_begin("http/URL 无 scheme 与端口边界");
    TF_CHECK_INT(http_parse_url("example.com/a/b", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_STR(host, "example.com");
    TF_CHECK_STR(path, "/a/b");
    TF_CHECK_INT(port, 80);

    TF_CHECK_INT(http_parse_url("http://h:99999/p", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_STR(host, "h");
    TF_CHECK_INT(port, 80);              /* 超出 65535 保留默认 */
    TF_CHECK_STR(path, "/p");

    TF_CHECK_INT(http_parse_url("http://h:0/p", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_INT(port, 80);              /* 0 视为无效 */

    TF_CHECK_INT(http_parse_url("http://h:abc/p", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_INT(port, 80);
    TF_CHECK_STR(path, "/p");
    tf_end();

    tf_begin("http/URL 输出缓冲截断");
    TF_CHECK_INT(http_parse_url("http://example.com/abcdef", host, 5,
                                &port, path, sizeof(path)), 0);
    TF_CHECK_STR(host, "exam");

    TF_CHECK_INT(http_parse_url("http://h/abcdef", host, sizeof(host),
                                &port, path, 4), 0);
    TF_CHECK_STR(path, "/ab");

    TF_CHECK_INT(http_parse_url("http://example.com:8080", host, sizeof(host),
                                &port, path, sizeof(path)), 0);
    TF_CHECK_INT(port, 8080);
    TF_CHECK_STR(path, "/");
    tf_end();

    tf_begin("http/URL 非法输入");
    TF_CHECK_INT(http_parse_url(NULL, host, sizeof(host), &port,
                                path, sizeof(path)), -1);
    TF_CHECK_INT(http_parse_url("http://h", NULL, 10, &port,
                                path, sizeof(path)), -1);
    TF_CHECK_INT(http_parse_url("http://h", host, sizeof(host), &port,
                                NULL, 10), -1);
    TF_CHECK_INT(http_parse_url("http://", host, sizeof(host), &port,
                                path, sizeof(path)), -1);
    TF_CHECK_INT(http_parse_url("", host, sizeof(host), &port,
                                path, sizeof(path)), -1);
    TF_CHECK_INT(http_parse_url("http://h", host, sizeof(host), &port,
                                path, 1), -1);
    TF_CHECK_INT(http_parse_url("http://h/p", host, sizeof(host), NULL,
                                path, sizeof(path)), 0);
    tf_end();
}

static void test_execute_ok(void)
{
    http_mock_t mock;
    http_transport_t transport;
    http_request_t req;
    http_response_t resp;

    tf_begin("http/POST 请求构造与响应解析");
    mock_reset(&mock);
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
    TF_CHECK_INT(mock.connect_calls, 1);
    TF_CHECK_STR(mock.host, "test.local");
    TF_CHECK_INT(mock.port, 80);
    TF_CHECK_INT(mock.timeout, 1000);
    TF_CHECK_INT(mock.close_calls, 1);
    TF_CHECK_STR(mock.req,
        "POST /x HTTP/1.1\r\nHost: test.local\r\nX-Test: 1\r\n"
        "Content-Length: 3\r\nConnection: close\r\n\r\nabc");
    TF_CHECK_INT(resp.status_code, 200);
    TF_CHECK_INT(resp.content_length, 5);
    TF_CHECK_INT(resp.body_len, 5);
    TF_CHECK_MEM(resp.body, "hello", 5);
    tf_end();

    tf_begin("http/默认方法 GET 与无 Content-Length");
    mock_reset(&mock);
    mock_set_response(&mock, "HTTP/1.1 404 Not Found\r\n\r\nnope");
    transport = make_transport(&mock);

    memset(&req, 0, sizeof(req));
    req.url = "http://test.local/missing";
    req.timeout = 1000;

    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK_STR(mock.req,
        "GET /missing HTTP/1.1\r\nHost: test.local\r\n"
        "Content-Length: 0\r\nConnection: close\r\n\r\n");
    TF_CHECK_INT(resp.status_code, 404);
    TF_CHECK_INT(resp.content_length, 0);
    TF_CHECK_INT(resp.body_len, 4);
    TF_CHECK_MEM(resp.body, "nope", 4);
    tf_end();

    tf_begin("http/空方法串回退为 GET");
    mock_reset(&mock);
    mock_set_response(&mock, "HTTP/1.1 200 OK\r\n\r\n");
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    req.method = "";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK(strncmp(mock.req, "GET / HTTP/1.1", 14) == 0);
    TF_CHECK_INT(resp.status_code, 200);
    TF_CHECK_INT(resp.body_len, 0);
    tf_end();

    tf_begin("http/状态行 1.0 与 204 无响应体");
    mock_reset(&mock);
    mock_set_response(&mock, "HTTP/1.0 204 No Content\r\n\r\n");
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK_INT(resp.status_code, 204);
    TF_CHECK_INT(resp.body_len, 0);
    tf_end();

    tf_begin("http/分片读取响应头");
    mock_reset(&mock);
    mock.max_chunk = 7;
    mock_set_response(&mock,
        "HTTP/1.1 201 Created\r\ncontent-length: 3\r\n\r\nxyz");
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/p";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK_INT(resp.status_code, 201);
    TF_CHECK_INT(resp.content_length, 3);
    TF_CHECK_INT(resp.body_len, 3);
    TF_CHECK_MEM(resp.body, "xyz", 3);
    TF_CHECK(mock.read_calls > 3);
    tf_end();
}

static void test_execute_body(void)
{
    http_mock_t mock;
    http_transport_t transport;
    http_request_t req;
    http_response_t resp;

    tf_begin("http/响应体比 Content-Length 短");
    mock_reset(&mock);
    mock_set_response(&mock,
        "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc");
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK_INT(resp.content_length, 10);
    TF_CHECK_INT(resp.body_len, 3);
    TF_CHECK_MEM(resp.body, "abc", 3);
    tf_end();

    tf_begin("http/响应体超过 Content-Length 时截断");
    mock_reset(&mock);
    mock_set_response(&mock,
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nabcd");
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK_INT(resp.body_len, 2);
    TF_CHECK_MEM(resp.body, "ab", 2);
    tf_end();

    tf_begin("http/Content-Length 带空格");
    mock_reset(&mock);
    mock_set_response(&mock,
        "HTTP/1.1 200 OK\r\nContent-Length:   7\r\n\r\n1234567");
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK_INT(resp.content_length, 7);
    TF_CHECK_INT(resp.body_len, 7);
    tf_end();

    tf_begin("http/body 为 NULL 时不发送响应体");
    mock_reset(&mock);
    mock_set_response(&mock, "HTTP/1.1 200 OK\r\n\r\n");
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    req.body = NULL;
    req.body_len = 9;
    TF_CHECK_INT(http_execute(&transport, &req, &resp), 0);
    TF_CHECK(mock.req_len == strlen(mock.req));
    TF_CHECK(strstr(mock.req, "Content-Length: 9\r\n") != NULL);
    tf_end();
}

static void test_execute_errors(void)
{
    http_mock_t mock;
    http_transport_t transport;
    http_request_t req;
    http_response_t resp;

    tf_begin("http/执行参数校验");
    mock_reset(&mock);
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    TF_CHECK_INT(http_execute(NULL, &req, &resp), -1);
    TF_CHECK_INT(http_execute(&transport, NULL, &resp), -1);
    TF_CHECK_INT(http_execute(&transport, &req, NULL), -1);
    {
        http_transport_t t = transport;
        t.connect = NULL;
        TF_CHECK_INT(http_execute(&t, &req, &resp), -1);
        t = transport;
        t.write = NULL;
        TF_CHECK_INT(http_execute(&t, &req, &resp), -1);
        t = transport;
        t.read = NULL;
        TF_CHECK_INT(http_execute(&t, &req, &resp), -1);
    }
    tf_end();

    tf_begin("http/连接失败");
    mock_reset(&mock);
    mock.fail_connect = 1;
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), -1);
    TF_CHECK_INT(mock.write_calls, 0);
    TF_CHECK_INT(mock.close_calls, 0);
    tf_end();

    tf_begin("http/URL 非法不发起连接");
    mock_reset(&mock);
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), -1);
    TF_CHECK_INT(mock.connect_calls, 0);
    tf_end();

    tf_begin("http/写失败关闭连接");
    mock_reset(&mock);
    mock.fail_write = 1;
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), -1);
    TF_CHECK_INT(mock.close_calls, 1);
    tf_end();

    tf_begin("http/响应头不完整");
    mock_reset(&mock);
    mock_set_response(&mock, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n");
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), -1);
    TF_CHECK_INT(mock.close_calls, 1);
    tf_end();

    tf_begin("http/空响应");
    mock_reset(&mock);
    transport = make_transport(&mock);
    memset(&req, 0, sizeof(req));
    req.url = "http://h/";
    TF_CHECK_INT(http_execute(&transport, &req, &resp), -1);
    tf_end();
}

void ut_http(void)
{
    test_parse_url();
    test_execute_ok();
    test_execute_body();
    test_execute_errors();
}
