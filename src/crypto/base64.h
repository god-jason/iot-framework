/*
 * base64.h - Base64 编解码接口
 *
 * 提供 RFC 4648 标准字母表的 Base64 编码/解码，无动态内存分配，
 * 输入输出缓冲区均由调用者提供。详细边界约束见 base64.c 文件头。
 */

#ifndef BASE64_H
#define BASE64_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 编码为 Base64（含 '=' 填充），output 以 '\0' 结尾。
 * 参数：input/input_len   - 待编码数据（input 不可为 NULL）
 *       output/output_size - 输出缓冲，容量至少 ((input_len + 2) / 3) * 4 + 1
 *       written            - 可为 NULL，返回编码字符数（不含 '\0'）
 * 返回：0 成功；-1 参数非法或空间不足
 */
int base64_encode(const void *input, size_t input_len,
                      char *output, size_t output_size,
                      size_t *written);

/*
 * 解码 Base64。
 * 参数：input/input_len   - Base64 文本（允许 \r\n 折行）
 *       output/output_size - 输出二进制缓冲及其容量
 *       written            - 可为 NULL，返回解码字节数
 * 返回：0 成功；-1 参数非法、含非法字符或空间不足
 * 注意：遇到 '=' 即停止解析，其后内容被忽略。
 */
int base64_decode(const char *input, size_t input_len,
                      void *output, size_t output_size,
                      size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* BASE64_H */
