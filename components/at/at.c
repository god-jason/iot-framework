/*
 * AT 指令事务引擎实现。
 *
 * 职责：
 *   维护一条「发送命令 -> 等待响应 -> 超时/重试 -> 回调结果」的事务
 *   状态机，并把 URC（非请求上报）从普通响应中分离出来单独分发。
 *
 * 数据流：
 *   at_process() 从 UART 泵取字节 -> at_parser_input() 切成行/提示符 ->
 *   at_on_token() 分类处理：URC 优先分发；否则匹配成功/失败终止符，
 *   其余数据行按 expect 过滤后追加到响应缓冲 -> at_complete() 置状态
 *   并回调用户。
 *
 * 依赖：
 *   port.h（UART 收发）、at_parser（行解析）、at_urc（URC 注册分发）。
 *
 * 静态状态与边界：
 *   - 全局单例，同一时刻只允许一条普通命令在途（s_req_pending）；
 *   - 响应缓冲固定 AT_RESPONSE_SIZE（256）字节，超长部分被截断；
 *   - s_tick_ms 是「逻辑 tick」，每次 at_process() 调用加 1，并非真实
 *     毫秒；因此 timeout 的计量单位是 at_process 的调用次数；
 *   - 只有在 at_start() 之后才会主动从 UART 泵取数据；
 *   - 成功/失败终止符使用子串匹配，数据行若包含 "OK" 等字样会被误判。
 */
#include "at.h"
#include "at_parser.h"
#include "at_urc.h"
#include "hal_mem.h"
#include <string.h>

/* 单条响应的累积缓冲大小（字节），超出后截断 */
#define AT_RESPONSE_SIZE 256

/* 全局配置副本（at_init 时整体拷贝） */
static at_config_t s_cfg;
/* 模块是否已初始化 */
static bool s_inited = false;
/* 是否已启动（决定 at_process 是否泵取 UART 数据） */
static bool s_started = false;

/* 当前事务状态机状态 */
static at_state_t s_state = AT_IDLE;
/* 当前（或最近一次）在途事务的请求参数 */
static at_request_t s_req;
/* 是否有事务正在等待响应 */
static bool s_req_pending = false;

/* 响应累积缓冲：多行以 '\n' 连接，始终以 '\0' 结尾；由 iot_mem 分配 */
static char *s_response = NULL;
/* 已写入响应缓冲的字节数（不含结尾 '\0'） */
static uint32_t s_response_len;

/* 逻辑 tick：每次 at_process 调用加 1，用于超时/重试计时 */
static uint32_t s_tick_ms;
/* 本次等待（或重发）开始的 tick 基准 */
static uint32_t s_start_ms;
/* 当前事务已重试次数 */
static uint8_t s_retried;

/* 最近一次事务的结果，供阻塞接口 at_transact() 返回 */
static at_result_t s_result = AT_RESULT_OK;

/* ---- 发送 ---- */

/*
 * 通过配置的 UART 发送原始字节。
 * 参数：data 数据指针；len 字节数。
 * 说明：data 为空、len 为 0 或未绑定 UART 时静默返回；发送超时固定 100ms。
 */
static void at_tx_bytes(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }
    if (s_cfg.uart != NULL) {
        hal_uart_transmit(s_cfg.uart, data, len, 100);
    }
}

/*
 * 发送以 '\0' 结尾的字符串。
 * 参数：s 字符串指针，可为 NULL。
 */
static void at_tx_str(const char *s)
{
    if (s == NULL) {
        return;
    }
    at_tx_bytes((const uint8_t *)s, (uint32_t)strlen(s));
}

/*
 * 发送一条 AT 命令：命令正文 + "\r\n"。
 * 参数：cmd 命令字符串。
 */
static void at_tx_command(const char *cmd)
{
    at_tx_str(cmd);
    at_tx_str("\r\n");
}

/* ---- 响应缓冲 ---- */

/* 确保响应缓冲已分配；失败返回 false（再次调用会重试） */
static bool at_response_ensure(void)
{
    if (s_response == NULL) {
        s_response = (char *)hal_calloc(AT_RESPONSE_SIZE, sizeof(char));
    }
    return (s_response != NULL);
}

/* 清空响应缓冲并写入结尾 '\0' */
static void at_response_reset(void)
{
    s_response_len = 0;
    if (s_response != NULL) {
        s_response[0] = '\0';
    }
}

/*
 * 向响应缓冲追加一行数据。
 * 参数：line 行内容（不含换行符）。
 * 说明：
 *   - 续行前补一个 '\n' 分隔；
 *   - 空间不足时按剩余容量截断，保证不越界且始终以 '\0' 结尾。
 */
static void at_response_append(const char *line)
{
    uint32_t l = (uint32_t)strlen(line);

    if (s_response == NULL) {
        return;   /* 缓冲不可用：丢弃数据 */
    }
    if (s_response_len > 0 && (s_response_len + 1) < AT_RESPONSE_SIZE) {
        s_response[s_response_len++] = '\n';
    }
    /* 预留 1 字节给结尾 '\0' */
    if (s_response_len + l >= AT_RESPONSE_SIZE) {
        l = AT_RESPONSE_SIZE - 1 - s_response_len;
    }
    memcpy(&s_response[s_response_len], line, l);
    s_response_len += l;
    s_response[s_response_len] = '\0';
}

/* ---- 完成事务 ---- */

/*
 * 结束当前事务并同步通知回调。
 * 参数：result 事务结果（OK 置 AT_COMPLETE，其余置 AT_ERROR）。
 * 说明：回调在调用者上下文（通常是 at_process）中同步执行；
 *       回调返回后清除 pending 标志，允许发起下一条事务。
 */
static void at_complete(at_result_t result)
{
    s_state = (result == AT_RESULT_OK) ? AT_COMPLETE : AT_ERROR;
    s_result = result;

    if (s_req.callback != NULL) {
        const char *response = (s_response != NULL) ? s_response : "";
        s_req.callback(result, response, s_response_len, s_req.user);
    }
    s_req_pending = false;
}

/* ---- 行处理 ---- */

/*
 * 处理解析器吐出的一整行。
 * 参数：line 行内容（已去 \r\n）；len 字节数。
 * 处理顺序：
 *   1. URC 优先：命中注册前缀则交给 URC 回调，不参与当前事务；
 *   2. 无在途事务时忽略该行；
 *   3. 子串匹配失败终止符（默认 "ERROR"）；
 *   4. 子串匹配成功终止符（默认 "OK"）；
 *   5. 数据行：配置了 expect 时仅保留包含该关键字的行。
 */
static void at_handle_line(const char *line, uint32_t len)
{
    const char *failure = s_req.failure ? s_req.failure : "ERROR";
    const char *success = s_req.success ? s_req.success : "OK";

    /* 1. URC 优先分发，不占普通命令事务 */
    if (at_urc_dispatch(line, len) == 0) {
        return;
    }
    if (!s_req_pending) {
        return;   /* 无在途命令：忽略噪声 */
    }

    /* 2. 失败终止 */
    if (strstr(line, failure) != NULL) {
        at_complete(AT_RESULT_ERROR);
        return;
    }
    /* 3. 成功终止 */
    if (strstr(line, success) != NULL) {
        at_complete(AT_RESULT_OK);
        return;
    }
    /* 4. 数据行（expect 过滤） */
    if (s_req.expect != NULL && strstr(line, s_req.expect) == NULL) {
        return;
    }
    at_response_append(line);
}

/*
 * 处理 ">" 数据模式提示符。
 * 说明：仅当请求的 success 被显式设置为 ">" 时，提示符才代表本阶段完成
 *       （例如 MQTT/HTTP 发送正文前的等待）；否则视为普通噪声忽略。
 */
static void at_handle_prompt(void)
{
    if (!s_req_pending) {
        return;
    }
    /* 以 ">" 为 success 时，提示符即完成当前阶段（如 MQTT/HTTP 发送前等待） */
    if (s_req.success != NULL && strcmp(s_req.success, ">") == 0) {
        at_complete(AT_RESULT_OK);
    }
}

/* ---- parser token 回调 ---- */

/*
 * 解析器 token 回调：把 token 分派到行/提示符/溢出处理。
 * 参数：token 解析结果；user 未使用（解析器注册时传 NULL）。
 */
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
        /* 行缓冲溢出说明响应异常，直接以溢出错误结束当前事务 */
        if (s_req_pending) {
            at_complete(AT_RESULT_OVERFLOW);
        }
        break;
    default:
        break;
    }
}

/* ---- 生命周期 ---- */

/*
 * 初始化 AT 引擎。
 * 参数：config 配置指针（非 NULL，且 uart 必须已绑定）。
 * 返回：0 成功；参数非法或内存分配失败返回 -1。
 * 说明：整体拷贝配置；若提供了 line_buffer 则同时初始化行解析器；
 *       此时不自动启动，需再调用 at_start()。
 */
int at_init(const at_config_t *config)
{
    if (config == NULL || config->uart == NULL) {
        return -1;
    }
    /* 重复初始化时先释放旧响应缓冲，再重新分配 */
    hal_free(s_response);
    s_response = NULL;
    if (!at_response_ensure()) {
        s_inited = false;
        return -1;   /* 内存分配失败 */
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
    return 0;
}

/*
 * 启动 AT 引擎（开始从 UART 泵取数据）。
 * 返回：0 成功；未初始化返回 -1。
 */
int at_start(void)
{
    if (!s_inited) {
        return -1;
    }
    s_started = true;
    s_state = AT_IDLE;
    s_req_pending = false;
    return 0;
}

/*
 * 停止 AT 引擎（不再泵取数据）。
 * 返回：恒为 0。
 * 说明：会清空在途事务标志，但不触发回调。
 */
int at_stop(void)
{
    s_started = false;
    s_state = AT_IDLE;
    s_req_pending = false;
    return 0;
}

/*
 * 反初始化 AT 引擎。
 * 返回：恒为 0。
 * 说明：清除初始化/启动标志和事务状态；不释放任何缓冲（由调用者管理）。
 */
int at_deinit(void)
{
    s_inited = false;
    s_started = false;
    s_state = AT_IDLE;
    s_req_pending = false;
    return 0;
}

/* ---- 发送 ---- */

/*
 * 仅发送一条 AT 命令（自动追加 "\r\n"），不建立事务、不等待响应。
 * 参数：command 命令字符串。
 * 返回：0 成功；未初始化或命令为空返回 -1。
 */
int at_send(const char *command)
{
    if (!s_inited || command == NULL) {
        return -1;
    }
    at_tx_command(command);
    return 0;
}

/*
 * 发送原始字节（不追加换行），常用于数据模式下的正文发送。
 * 参数：data 数据指针；len 字节数。
 * 返回：0 成功；未初始化或参数非法返回 -1。
 */
int at_send_raw(const uint8_t *data, uint32_t len)
{
    if (!s_inited || data == NULL || len == 0) {
        return -1;
    }
    at_tx_bytes(data, len);
    return 0;
}

/* ---- 事务 ---- */

/*
 * 非阻塞提交一条事务。
 * 参数：request 请求参数（非 NULL），其中 command 可为 NULL（仅等待响应）。
 * 返回：0 已提交；未初始化、参数为空或已有事务在途返回 -1。
 * 说明：
 *   - 提交后立即发送命令并把状态推进到 AT_WAITING；
 *   - 结果通过 request->callback 通知（可为 NULL）；
 *   - 超时时间取 request->timeout，为 0 时回退到 default_timeout。
 */
int at_query(const at_request_t *request)
{
    if (!s_inited || request == NULL) {
        return -1;
    }
    if (s_req_pending) {
        return -1;   /* 单事务模型：同一时刻只允许一条在途命令 */
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
    return 0;
}

/*
 * 阻塞式完成一个事务，直到收到结果。
 * 参数：request 请求参数。
 * 返回：事务结果；提交失败（如已有事务在途）返回 AT_RESULT_BUSY。
 * 说明：循环调用 at_process() 推进接收与超时；会阻塞当前执行流。
 */
at_result_t at_transact(const at_request_t *request)
{
    if (at_query(request) != 0) {
        return AT_RESULT_BUSY;
    }
    /* 阻塞等待：接收泵取与超时由 at_process() 完成 */
    while (s_req_pending) {
        at_process();
    }
    return s_result;
}

/*
 * 阻塞式执行一条简单命令（成功 "OK"、失败 "ERROR"）。
 * 参数：command 命令字符串；timeout 超时（逻辑 tick 数，0 表示用默认值）。
 * 返回：事务结果。
 */
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

/*
 * 取消当前事务。
 * 返回：0 已取消；当前无在途事务返回 -1。
 * 说明：以 AT_RESULT_CANCELLED 结束并触发回调。
 */
int at_cancel(void)
{
    if (!s_req_pending) {
        return -1;
    }
    at_complete(AT_RESULT_CANCELLED);
    return 0;
}

/* ---- 输入与轮询 ---- */

/*
 * 向解析器注入接收数据。
 * 参数：data 数据指针；len 字节数。
 * 说明：未初始化时忽略；适用于 UART 中断/DMA 回调中直接喂数据。
 */
void at_input(const uint8_t *data, uint32_t len)
{
    if (s_inited) {
        at_parser_input(data, len);
    }
}

/*
 * 主循环轮询：泵取 UART 接收数据并检查超时/重试。
 * 返回：恒为 0。
 * 说明：
 *   - 每次调用先令 s_tick_ms 自增，作为逻辑计时单位；
 *   - 仅在 s_started 且配置了 rx_buffer 时才主动读取 UART；
 *   - 单次最多读取 rx_buffer_size 字节，循环直至读空；
 *   - 超时后若仍有重试次数则重发命令，否则以 AT_RESULT_TIMEOUT 结束。
 */
int at_process(void)
{
    uint32_t n;
    hal_status_t r;

    s_tick_ms++;

    /* 从 UART 泵取接收数据 */
    if (s_started && s_cfg.uart != NULL &&
        s_cfg.rx_buffer != NULL && s_cfg.rx_buffer_size > 0) {

        n = (uint32_t)hal_uart_get_rx_count(s_cfg.uart);
        while (n > 0) {
            uint32_t chunk = (n > s_cfg.rx_buffer_size)
                             ? s_cfg.rx_buffer_size : n;
            r = hal_uart_receive(s_cfg.uart, s_cfg.rx_buffer, chunk, 100);
            if (r != HAL_OK) {
                break;   /* 读取失败：本轮放弃，等待下次轮询 */
            }
            at_input(s_cfg.rx_buffer, chunk);
            n = (uint32_t)hal_uart_get_rx_count(s_cfg.uart);
        }
    }

    /* 超时 / 重试检测 */
    if (s_req_pending && s_state == AT_WAITING) {
        /* timeout 为 0 时回退到全局默认超时 */
        uint32_t timeout = s_req.timeout ? s_req.timeout : s_cfg.default_timeout;
        if (timeout > 0 && (s_tick_ms - s_start_ms) >= timeout) {
            if (s_retried < s_req.retry) {
                /* 仍有重试额度：重发命令并重置计时基准 */
                s_retried++;
                at_tx_command(s_req.command);
                s_start_ms = s_tick_ms;
            } else {
                at_complete(AT_RESULT_TIMEOUT);
            }
        }
    }
    return 0;
}

/*
 * 查询是否有事务在途。
 * 返回：true 表示正在等待响应。
 */
bool at_is_busy(void)
{
    return s_req_pending;
}

/*
 * 获取当前状态机状态。
 * 返回：at_state_t 状态值（AT_IDLE/AT_SENDING/AT_WAITING/...）。
 */
at_state_t at_get_state(void)
{
    return s_state;
}

/*
 * 获取最近一次事务累积的响应文本。
 * 返回：内部静态缓冲指针（以 '\0' 结尾），下次事务开始时被清空。
 * 注意：指针生命周期由本模块管理，调用者不应修改或释放。
 */
const char *at_get_response(void)
{
    if (!at_response_ensure()) {
        return NULL;   /* 内存分配失败 */
    }
    return s_response;
}