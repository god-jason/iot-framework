/*
 * hex.h - 十六进制字符串编解码接口
 *
 * 编码输出长度固定为 input_len * 2，并额外占用 1 字节 '\0'；
 * 解码要求输入长度为偶数且字符合法。详细约束见 hex.c 文件头。
 */

#ifndef HEX_H
#define HEX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 将字节流编码为十六进制字符串；output 会以 '\0' 结尾。
 * 参数：input/input_len   - 输入数据及字节数
 *       output/output_size - 输出缓冲，容量至少 input_len * 2 + 1
 *       uppercase          - true 输出大写 A-F，false 输出小写 a-f
 * 返回：0 成功；-1 参数非法或空间不足
 */
int hex_encode(const void *input, size_t input_len,
                   char *output, size_t output_size,
                   bool uppercase);

/*
 * 将十六进制字符串解码为字节流；written 返回实际解码字节数。
 * 参数：input/input_len   - 十六进制文本及字符数（必须为偶数）
 *       output/output_size - 输出缓冲，容量至少 input_len / 2
 *       written            - 可为 NULL，返回解码字节数
 * 返回：0 成功；-1 参数非法、长度为奇数、含非法字符或空间不足
 */
int hex_decode(const char *input, size_t input_len,
                   void *output, size_t output_size,
                   size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* HEX_H */
