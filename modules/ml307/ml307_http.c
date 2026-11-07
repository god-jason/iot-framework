#include "ml307_http.h"
#include "at.h"

#include <stdio.h>
#include <string.h>

#define ML307_HTTP_RESP_MAX 512

static char s_http_resp[ML307_HTTP_RESP_MAX];

static int http_read(const char **resp, uint32_t *resp_len)
{
    const char *r;

    if (at_exec("AT+HTTPREAD=0,1024", 5000) != AT_RESULT_OK) {
        return -1;
    }
    r = at_get_response();
    if (r == NULL) {
        return -1;
    }
    strncpy(s_http_resp, r, sizeof(s_http_resp) - 1);
    s_http_resp[sizeof(s_http_resp) - 1] = '\0';

    if (resp != NULL) {
        *resp = s_http_resp;
    }
    if (resp_len != NULL) {
        *resp_len = (uint32_t)strlen(s_http_resp);
    }
    return 0;
}

int ml307_http_get(const char *url, const char **resp, uint32_t *resp_len)
{
    char cmd[320];

    if (url == NULL || url[0] == '\0') {
        return -1;
    }
    (void)at_exec("AT+HTTPTERM", 2000);

    snprintf(cmd, sizeof(cmd), "AT+HTTPGET=%s", url);
    if (at_exec(cmd, 10000) != AT_RESULT_OK) {
        return -1;
    }
    return http_read(resp, resp_len);
}

int ml307_http_post(const char *url, const char *body,
                        const char **resp, uint32_t *resp_len)
{
    char cmd[320];
    at_request_t phase;

    if (url == NULL || url[0] == '\0') {
        return -1;
    }
    (void)at_exec("AT+HTTPTERM", 2000);
    (void)at_exec("AT+HTTPHEADER=\"Content-Type: application/json\"", 2000);

    snprintf(cmd, sizeof(cmd), "AT+HTTPPOST=%s", url);

    /* 阶段1：发送 POST 并等待 > 提示符 */
    memset(&phase, 0, sizeof(phase));
    phase.command = cmd;
    phase.success = ">";
    phase.failure = "ERROR";
    phase.timeout = 5000;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return -1;
    }

    /* 阶段2：发送请求体 */
    if (body != NULL) {
        at_send_raw((const uint8_t *)body, (uint32_t)strlen(body));
    }
    at_send_raw((const uint8_t *)"\r\n", 2);

    /* 阶段3：等待 OK */
    memset(&phase, 0, sizeof(phase));
    phase.command = NULL;
    phase.success = "OK";
    phase.failure = "ERROR";
    phase.timeout = 5000;
    if (at_transact(&phase) != AT_RESULT_OK) {
        return -1;
    }

    return http_read(resp, resp_len);
}