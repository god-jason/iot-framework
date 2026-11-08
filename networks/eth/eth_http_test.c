/* ETH HTTP 功能测试：本机 Mongoose HTTP 服务端 + iot_http 客户端。 */
#include "test.h"
#include "iot_http.h"
#include "eth_net.h"

#include <string.h>

static void http_server_fn(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        if (mg_match(hm->uri, mg_str("/echo"), NULL)) {
            mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "%.*s",
                          (int)hm->body.len, hm->body.buf);
        } else {
            mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "hello-http");
        }
    }
}

void ut_eth_http(void)
{
    struct mg_connection *server;
    iot_http_request_t request;
    iot_http_response_t response;
    uint8_t body[64];

    tf_begin("eth_http/GET 请求");
    TF_CHECK_INT(iot_http_init(), HAL_OK);
    server = mg_http_listen(eth_net_mgr(), "http://127.0.0.1:18080",
                            http_server_fn, NULL);
    TF_CHECK(server != NULL);

    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    request.url = "http://127.0.0.1:18080/hello";
    request.method = IOT_HTTP_GET;
    request.timeout = 2000U;
    response.body = body;
    response.body_cap = sizeof(body);

    TF_CHECK_INT(iot_http_request(&request, &response), HAL_OK);
    TF_CHECK_INT(response.status_code, 200);
    TF_CHECK_INT(response.body_len, 10);
    TF_CHECK_MEM(response.body, "hello-http", 10);
    tf_end();

    tf_begin("eth_http/POST 回显");
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    request.url = "http://127.0.0.1:18080/echo";
    request.method = IOT_HTTP_POST;
    request.body = (const uint8_t *)"ping-body";
    request.body_len = 9U;
    request.timeout = 2000U;
    response.body = body;
    response.body_cap = sizeof(body);

    TF_CHECK_INT(iot_http_request(&request, &response), HAL_OK);
    TF_CHECK_INT(response.status_code, 200);
    TF_CHECK_INT(response.body_len, 9);
    TF_CHECK_MEM(response.body, "ping-body", 9);
    tf_end();

    server->is_closing = 1;
    eth_net_poll(0);
    TF_CHECK_INT(iot_http_deinit(), HAL_OK);
}
