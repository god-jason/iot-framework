#ifndef AT_PARSER_H
#define AT_PARSER_H

/*
 * AT 响应流解析器（单例）。
 * 把串口字节流切成「行」与「> 提示符」两类 token，供 AT 引擎消费。
 * 处理要点：一条响应分多次到达、一次到达多条、\r\n 行边界、> 数据模式提示符、缓冲溢出。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AT_LINE,      /* 一个完整行（已去掉 \r\n） */
    AT_PROMPT,    /* ">" 数据模式提示符 */
    AT_BINARY,    /* 二进制数据块（预留） */
    AT_OVERFLOW   /* 行缓冲溢出 */
} at_token_type_t;

typedef struct {
    at_token_type_t type;
    const uint8_t *data;
    uint32_t len;
} at_token_t;

typedef void (*at_token_fn)(const at_token_t *token, void *user);

int at_parser_init(void *buffer, uint32_t size,
                       at_token_fn callback, void *user);

int at_parser_input(const uint8_t *data, uint32_t len);
int at_parser_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_PARSER_H */