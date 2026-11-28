/*
 * bcd.h - 压缩 BCD（每字节两位十进制数）编解码接口
 *
 * 编码采用右对齐方式：最低位数字位于最后一个字节的低半字节，高位补 0。
 * 详细约束见 bcd.c 文件头。
 */

#ifndef BCD_H
#define BCD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 将数值编码为压缩 BCD（每字节存两个十进制数字，高位在前）。
 * 参数：value      - 待编码数值
 *       output     - 输出缓冲区（不可为 NULL）
 *       output_len - 输出字节数，可容纳 output_len * 2 位十进制数
 * 返回：0 成功；-1 参数非法或十进制位数超出容量
 */
int bcd_encode(uint32_t value, uint8_t *output, uint32_t output_len);

/*
 * 将压缩 BCD 解码为数值。
 * 参数：input - BCD 数据；input_len - 字节数；value - 输出数值
 * 返回：0 成功；-1 参数非法或遇到非法 BCD（半字节 > 9）
 * 注意：不做溢出检查。
 */
int bcd_decode(const uint8_t *input, uint32_t input_len, uint32_t *value);

/*
 * 校验数据是否为合法 BCD（每个半字节均 <= 9）。
 * 参数：data - 数据首地址；len - 字节数
 * 返回：0 合法；-1 非法（data 为 NULL 且 len 非 0 也算非法）
 */
int bcd_is_valid(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* BCD_H */
