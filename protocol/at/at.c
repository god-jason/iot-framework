#include "at.h"
#include "at_parser.h"
#include "at_urc.h"
#include "iot_string.h"

#include <string.h>

#define AT_RESPONSE_SIZE 256

static at_config_t s_cfg;
static bool s_inited = false;
static bool s_started = false;

static at_state_t s_state = AT_IDLE;
static at_request_t s_req;
static bool s_req_pending = false;

static char s_response[AT_RESPONSE_SIZE];
static uint32_t s_response_len;

static uint32_t s_tick_ms;
static uint32_t s_start_ms;
static uint8_t s_retried;

static at_result_t s_result = AT_RESULT_OK;

/* ---- 发送 ---- */
static void at_tx_bytes(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }
    if (s_cfg.uart != NULL && s_cfg.uart->send != NULL) {
        s_cfg.uart->send(s_cfg.uart->ctx, data, len);
    }
}

static void at_tx_str(const char *s)
{
    if (s == NULL) {
        return;
    }
    at_tx_bytes((const uint8_t *)s, (uint32_t)iot_string_length(s));
}

static void at_tx_command(const char *cmd)
{
    at_tx_str(cmd);
    at_tx_str("\r\n");
}

/* ---- 响应缓冲 ---- */
static void at_response_reset(void)
{
    s_response_len = 0;
    s_response[0] = '\0';
}

static void at_response_append(const char *line)
{
    uint32_t l = (uint32_t)iot_string_length(line);

    if (s_response_len > 0 && (s_response_len + 1) < AT_RESPONSE_SIZE) {
        s_response[s_response_len++] = '\n';
    }
    if (s_response_len + l >= AT_RESPONSE_SIZE) {
        l = AT_RESPONSE_SIZE - 1 - s_response_len;
    }
    memcpy(&s_response[s_response_len], line, l);
    s_response_len += l;
    s_response[s_response_len] = '\0';
}

/* ---- 完成事务 ---- */
static void at_complete(at_result_t result)
{
    s_state = (result == AT_RESULT_OK) ? AT_COMPLETE : AT_ERROR;
    s_result = result;

    if (s_req.callback != NULL) {
        s_req.callback(result, s_response, s_response_len, s_req.user);
    }
    s_req_pending = false;
}

/* ---- 行处理 ---- */
static void at_handle_line(const char *line, uint32_t len)
{
    const char *failure = s_req.failure ? s_req.failure : "ERROR";
    const char *success = s_req.success ? s_req.success : "OK";

    /* 1. URC 优先分发，不占普通命令事务 */
    if (at_urc_dispatch(line, len) == IOT_OK) {
        return;
    }
    if (!s_req_pending) {
        return;   /* 无在途命令：忽略噪声 */
    }

    /* 2. 失败终止 */
    if (iot_string_find(line, failure) != NULL) {
        at_complete(AT_RESULT_ERROR);
        return;
    }
    /* 3. 成功终止 */
    if (iot_string_find(line, success) != NULL) {
        at_complete(AT_RESULT_OK);
        return;
    }
    /* 4. 数据行（expect 过滤） */
    if (s_req.expect != NULL && iot_string_find(line, s_req.expect) == NULL) {
        return;
    }
    at_response_append(line);
}

static void at_handle_prompt(void)
{
    if (!s_req_pending) {
        return;
    }
    /* 以 ">" 为 success 时，提示符即完成当前阶段（如 MQTT/HTTP 发送前等待） */
    if (s_req.success != NULL && iot_string_compare(s_req.success, ">") == 0) {
        at_complete(AT_RESULT_OK);
    }
}

/* ---- parser token 回调 ---- */
static void at_on_token(const at_token_t *token, void *user)
{
    (void)user;
    switch (token->type) {
    case AT_LINE:
        at_handle_line((const char *)token->data, token->len);
        break;
    case AT_PROMPT:
        at_handle_prompt();
        break;
    case AT_OVERFLOW:
        if (s_req_pending) {
            at_complete(AT_RESULT_OVERFLOW);
        }
        break;
    default:
        break;
    }
}

/* ---- 生命周期 ---- */
int at_init(const at_config_t *config)
{
    if (config == NULL || config->uart == NULL) {
        return -1;
    }
    s_cfg = *config;
    s_inited = true;
    s_started = false;
    s_state = AT_IDLE;
    s_req_pending = false;
    s_tick_ms = 0;

    at_response_reset();

    /* 用配置的 line_buffer 初始化解析器 */
    if (s_cfg.line_buffer != NULL && s_cfg.line_buffer_size > 0) {
        at_parser_init(s_cfg.line_buffer, s_cfg.line_buffer_size,
                           at_on_token, NULL);
    }
    return IOT_OK;
}

int at_start(void)
{
    if (!s_inited) {
        return -1;
    }
    s_started = true;
    s_state = AT_IDLE;
    s_req_pending = false;
    return IOT_OK;
}

int at_stop(void)
{
    s_started = false;
    s_state = AT_IDLE;
    s_req_pending = false;
    return IOT_OK;
}

int at_deinit(void)
{
    s_inited = false;
    s_started = false;
    s_state = AT_IDLE;
    s_req_pending = false;
    return IOT_OK;
}

/* ---- 发送 ---- */
int at_send(const char *command)
{
    if (!s_inited || command == NULL) {
        return -1;
    }
    at_tx_command(command);
    return IOT_OK;
}

int at_send_raw(const uint8_t *data, uint32_t len)
{
    if (!s_inited || data == NULL || len == 0) {
        return -1;
    }
    at_tx_bytes(data, len);
    return IOT_OK;
}

/* ---- 事务 ---- */
int at_query(const at_request_t *request)
{
    if (!s_inited || request == NULL) {
        return -1;
    }
    if (s_req_pending) {
        return -1;
    }

    s_req = *request;
    at_response_reset();
    s_retried = 0;
    s_req_pending = true;
    s_state = AT_SENDING;

    if (request->command != NULL) {
        at_tx_command(request->command);
    }

    s_state = AT_WAITING;
    s_start_ms = s_tick_ms;
    return IOT_OK;
}

at_result_t at_transact(const at_request_t *request)
{
    if (at_query(request) != IOT_OK) {
        return AT_RESULT_BUSY;
    }
    /* 阻塞等待：接收泵取与超时由 at_process() 完成 */
    while (s_req_pending) {
        at_process();
    }
    return s_result;
}

at_result_t at_exec(const char *command, uint32_t timeout)
{
    at_request_t req;

    memset(&req, 0, sizeof(req));
    req.command = command;
    req.success = "OK";
    req.failure = "ERROR";
    req.timeout = timeout;
    req.callback = NULL;

    return at_transact(&req);
}

int at_cancel(void)
{
    if (!s_req_pending) {
        return -1;
    }
    at_complete(AT_RESULT_CANCELLED);
    return IOT_OK;
}

/* ---- 输入与轮询 ---- */
void at_input(const uint8_t *data, uint32_t len)
{
    if (s_inited) {
        at_parser_input(data, len);
    }
}

int at_process(void)
{
    uint32_t n;

    s_tick_ms++;

    /* 从 UART 泵取接收数据 */
    if (s_started && s_cfg.uart != NULL &&
        s_cfg.uart->available != NULL &&
        s_cfg.uart->receive != NULL &&
        s_cfg.rx_buffer != NULL && s_cfg.rx_buffer_size > 0) {

        n = s_cfg.uart->available(s_cfg.uart->ctx);
        while (n > 0) {
            uint32_t chunk = (n > s_cfg.rx_buffer_size)
                             ? s_cfg.rx_buffer_size : n;
            int r = s_cfg.uart->receive(s_cfg.uart->ctx, s_cfg.rx_buffer, chunk);
            if (r <= 0) {
                break;
            }
            at_input(s_cfg.rx_buffer, (uint32_t)r);
            n = s_cfg.uart->available(s_cfg.uart->ctx);
        }
    }

    /* 超时 / 重试检测 */
    if (s_req_pending && s_state == AT_WAITING) {
        uint32_t timeout = s_req.timeout ? s_req.timeout : s_cfg.default_timeout;
        if (timeout > 0 && (s_tick_ms - s_start_ms) >= timeout) {
            if (s_retried < s_req.retry) {
                s_retried++;
                at_tx_command(s_req.command);
                s_start_ms = s_tick_ms;
            } else {
                at_complete(AT_RESULT_TIMEOUT);
            }
        }
    }
    return IOT_OK;
}

bool at_is_busy(void)
{
    return s_req_pending;
}

at_state_t at_get_state(void)
{
    return s_state;
}

const char *at_get_response(void)
{
    return s_response;
}