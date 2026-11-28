#ifndef AT_PARSER_H
#define AT_PARSER_H

/*
 * AT 响应流解析器（单例）。
 * 把串口字节流切成「行」与「> 提示符」两类 token，供 AT 引擎消费。
 * 处理要点：一条响应分多次到达、一次到达多条、\r\n 行边界、> 数据模式提示符、缓冲溢出。
 *
 * 模块说明：
 *   - 模块内只有一份解析状态，不支持多个串口同时解析；
 *   - 行缓冲由调用者提供，本模块不分配内存；
 *   - 回调收到的 token.data 指向内部行缓冲，仅在回调执行期间有效。
 * 边界约束：
 *   - 单行最大长度 = size - 1（预留 '\0'），超长时抛出 AT_OVERFLOW；
 *   - 仅以 '\n' 作为行结束符，'\r' 被忽略；
 *   - 行首（本行尚未累积字符）的 '>' 才被识别为提示符。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* token 类型 */
typedef enum {
    AT_LINE,      /* 一个完整行（已去掉 \r\n） */
    AT_PROMPT,    /* 行首的 ">" 数据模式提示符 */
    AT_BINARY,    /* 二进制数据块（预留，当前未产生） */
    AT_OVERFLOW   /* 行缓冲溢出，当前行已丢弃 */
} at_token_type_e;

/* 解析得到的一个 token */
typedef struct {
    at_token_type_e type;   /* token 类型 */
    const uint8_t *data;    /* 数据指针；AT_PROMPT/AT_OVERFLOW 为 NULL */
    uint32_t len;           /* 数据长度；AT_PROMPT/AT_OVERFLOW 为 0 */
} at_token_t;

/*
 * token 回调。
 * 参数：token 解析结果；user 注册时传入的用户指针。
 */
typedef void (*at_token_fn)(const at_token_t *token, void *user);

/*
 * 初始化解析器。
 * 参数：buffer 行缓冲；size 缓冲字节数；
 *       callback token 回调（非 NULL）；user 用户指针。
 * 返回：0 成功；参数非法返回 -1。
 */
int at_parser_init(void *buffer, uint32_t size,
                       at_token_fn callback, void *user);

/*
 * 输入字节流并完成切分。
 * 参数：data 数据指针；len 字节数（为 0 或 data 为 NULL 时直接返回）。
 * 返回：0。
 */
int at_parser_input(const uint8_t *data, uint32_t len);

/* 复位行缓冲，丢弃尚未收尾的行；恒返回 0 */
int at_parser_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_PARSER_H */