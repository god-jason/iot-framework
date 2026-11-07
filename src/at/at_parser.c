#include "at_parser.h"

static uint8_t *s_buf;
static uint32_t s_size;
static uint32_t s_len;
static at_token_fn s_cb;
static void *s_user;

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
    return IOT_OK;
}

int at_parser_reset(void)
{
    s_len = 0;
    return IOT_OK;
}

int at_parser_input(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL || len == 0) {
        return IOT_OK;
    }

    for (i = 0; i < len; i++) {
        uint8_t c = data[i];

        if (c == '\n') {
            /* 行结束 */
            if (s_len > 0) {
                if (s_buf[s_len - 1] == '\r') {
                    s_len--;
                }
                if (s_len > 0) {
                    s_buf[s_len] = '\0';
                    parser_emit(AT_LINE, s_buf, s_len);
                }
            }
            s_len = 0;
        } else if (c == '>') {
            /* 行首的 > 视为数据模式提示符 */
            if (s_len == 0) {
                parser_emit(AT_PROMPT, NULL, 0);
            } else if (s_len < s_size - 1u) {
                s_buf[s_len++] = c;
            } else {
                parser_emit(AT_OVERFLOW, NULL, 0);
                s_len = 0;
            }
        } else if (c == '\r') {
            /* 忽略 CR，等待 LF 统一收尾 */
        } else if (s_len < s_size - 1u) {
            s_buf[s_len++] = c;
        } else {
            parser_emit(AT_OVERFLOW, NULL, 0);
            s_len = 0;
        }
    }
    return IOT_OK;
}