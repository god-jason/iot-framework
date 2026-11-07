/*
 * AT 响应流解析器实现（单例）。
 *
 * 职责：
 *   把 UART 收到的任意分片字节流切分为「完整行」与「> 提示符」两类 token，
 *   通过回调交给 AT 引擎消费。
 *
 * 数据流：
 *   at_parser_input() 逐字节扫描 -> 遇 '\n' 收尾一行（自动去掉结尾 '\r'）
 *   -> 行首的 '>' 作为提示符事件 -> 其余可打印字节累积到行缓冲
 *   -> 缓冲写满时上报 AT_OVERFLOW 并清空，防止粘包。
 *
 * 静态状态与边界：
 *   - 全模块只有一份状态（s_buf/s_size/s_len/s_cb/s_user），不支持多实例；
 *   - 行缓冲由调用者提供，最大可用长度为 size-1（需预留结尾 '\0'）；
 *   - 返回的行数据指向内部缓冲，回调内有效，回调返回后可能被覆盖；
 *   - '\r' 被单独忽略，统一以 '\n' 作为行结束标志。
 */
#include "at_parser.h"

/* 调用者提供的行缓冲及其容量 */
static uint8_t *s_buf;
static uint32_t s_size;
/* 当前行已累积字节数（不含结尾 '\0'） */
static uint32_t s_len;
/* token 回调与用户指针 */
static at_token_fn s_cb;
static void *s_user;

/*
 * 构造 token 并同步回调。
 * 参数：type token 类型；data 数据指针（可为 NULL）；len 数据长度。
 * 说明：未注册回调时直接返回。
 */
static void parser_emit(at_token_type_t type, const uint8_t *data, uint32_t len)
{
    at_token_t token;

    if (s_cb == NULL) {
        return;
    }
    token.type = type;
    token.data = data;
    token.len = len;
    s_cb(&token, s_user);
}

/*
 * 初始化解析器（单例）。
 * 参数：buffer 行缓冲；size 缓冲字节数；callback token 回调；user 用户指针。
 * 返回：0 成功；任一参数非法返回 -1。
 * 说明：缓冲容量至少 2 字节才能容纳 1 个字符 + 结尾 '\0'。
 */
int at_parser_init(void *buffer, uint32_t size,
                       at_token_fn callback, void *user)
{
    if (buffer == NULL || size == 0 || callback == NULL) {
        return -1;
    }
    s_buf = (uint8_t *)buffer;
    s_size = size;
    s_cb = callback;
    s_user = user;
    s_len = 0;
    return 0;
}

/* 复位行缓冲（丢弃未完成的行）：恒返回 0 */
int at_parser_reset(void)
{
    s_len = 0;
    return 0;
}

/*
 * 输入任意长度的字节流并完成切分。
 * 参数：data 数据指针；len 字节数。
 * 返回：0（参数为 NULL 或长度为 0 时也直接返回）。
 * 说明：
 *   - 支持一条响应分多次到达、一次到达多条响应的情形；
 *   - 行缓冲满时上报 AT_OVERFLOW 并清空，避免后续数据全部错位。
 */
int at_parser_input(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL || len == 0) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        uint8_t c = data[i];

        if (c == '\n') {
            /* 行结束：先去掉可能存在的结尾 '\r' */
            if (s_len > 0) {
                if (s_buf[s_len - 1] == '\r') {
                    s_len--;
                }
                /* 纯 "\r\n" 空行不产生 token */
                if (s_len > 0) {
                    s_buf[s_len] = '\0';
                    parser_emit(AT_LINE, s_buf, s_len);
                }
            }
            s_len = 0;
        } else if (c == '>') {
            /* 行首的 '>' 视为数据模式提示符 */
            if (s_len == 0) {
                parser_emit(AT_PROMPT, NULL, 0);
            } else if (s_len < s_size - 1u) {
                /* 行中间的 '>' 属于普通内容 */
                s_buf[s_len++] = c;
            } else {
                parser_emit(AT_OVERFLOW, NULL, 0);
                s_len = 0;
            }
        } else if (c == '\r') {
            /* 忽略 CR，等待 LF 统一收尾 */
        } else if (s_len < s_size - 1u) {
            /* 预留 1 字节给结尾 '\0' */
            s_buf[s_len++] = c;
        } else {
            /* 行过长：上报溢出并丢弃当前行 */
            parser_emit(AT_OVERFLOW, NULL, 0);
            s_len = 0;
        }
    }
    return 0;
}