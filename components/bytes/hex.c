/*
 * hex.c - 十六进制字符串编解码
 *
 * 模块职责：
 *   在二进制数据与可打印的十六进制文本之间转换，供日志打印、
 *   配置项显示、调试命令行等场景使用。
 *
 * 数据流与依赖：
 *   纯计算模块，无全局状态、无动态内存，仅依赖 标准头文件。
 *
 * 边界约束：
 *   - 编码固定输出小写或大写字母（由 uppercase 决定），
 *     输出长度为 input_len * 2，另需 1 字节容纳 '\0'。
 *   - 解码要求输入长度为偶数且全部为合法十六进制字符，否则整体失败；
 *     支持大小写混排。
 */

#include "hex.h"

/*
 * 把一个半字节（0..15）转换为十六进制字符。
 * 参数：v - 半字节值；uppercase - true 输出大写 A-F，false 输出小写 a-f
 * 返回：对应字符；调用者需保证 v <= 15
 */
static char hex_nibble(uint8_t v, bool uppercase)
{
    if (v < 10) {
        return (char)('0' + v);
    }
    return (char)((uppercase ? 'A' : 'a') + (v - 10));
}

/*
 * 把单个十六进制字符转换为半字节值。
 * 参数：c - 输入字符
 * 返回：0..15 表示有效字符；-1 表示非法字符（含 'x'、'0x' 前缀等）
 */
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/*
 * 将字节流编码为十六进制字符串。
 * 参数：
 *   input       - 待编码数据（不可为 NULL）
 *   input_len   - 待编码字节数
 *   output      - 输出字符串缓冲，成功时以 '\0' 结尾
 *   output_size - 输出缓冲容量，至少为 input_len * 2 + 1
 *   uppercase   - true 使用大写字母，false 使用小写字母
 * 返回：0 成功；-1 参数非法或空间不足
 * 注意：本函数不返回实际写入长度，可按 input_len * 2 计算。
 */
int hex_encode(const void *input, size_t input_len,
                   char *output, size_t output_size,
                   bool uppercase)
{
    const uint8_t *src = (const uint8_t *)input;
    size_t i;

    if (input == NULL || output == NULL) {
        return -1;
    }
    if (output_size < (input_len * 2u + 1u)) {
        return -1;
    }
    for (i = 0; i < input_len; i++) {
        /* 每字节拆成高、低半字节，各对应一个字符 */
        output[i * 2u]     = hex_nibble((uint8_t)(src[i] >> 4), uppercase);
        output[i * 2u + 1] = hex_nibble((uint8_t)(src[i] & 0x0F), uppercase);
    }
    output[input_len * 2u] = '\0';
    return 0;
}

/*
 * 将十六进制字符串解码为字节流。
 * 参数：
 *   input       - 十六进制文本（不可为 NULL）
 *   input_len   - 字符数，必须为偶数
 *   output      - 输出二进制缓冲
 *   output_size - 输出缓冲容量，至少为 input_len / 2
 *   written     - 可选（可为 NULL），返回实际解码字节数（= input_len / 2）
 * 返回：0 成功；-1 参数非法、长度为奇数、含非法字符或空间不足
 * 注意：遇到第一个非法字符即返回 -1，此前已写入的字节不会回滚。
 */
int hex_decode(const char *input, size_t input_len,
                   void *output, size_t output_size,
                   size_t *written)
{
    uint8_t *dst = (uint8_t *)output;
    size_t i;

    if (input == NULL || output == NULL) {
        return -1;
    }
    /* 每两个字符组成一个字节，奇数长度无法配对 */
    if ((input_len & 1u) != 0) {
        return -1;
    }
    if (output_size < (input_len / 2u)) {
        return -1;
    }
    for (i = 0; i < input_len / 2u; i++) {
        int hi = hex_value(input[i * 2u]);
        int lo = hex_value(input[i * 2u + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        dst[i] = (uint8_t)((hi << 4) | lo);
    }
    if (written != NULL) {
        *written = input_len / 2u;
    }
    return 0;
}
