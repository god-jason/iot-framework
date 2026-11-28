/*
 * bcd.c - 压缩 BCD（Binary-Coded Decimal）编解码
 *
 * 模块职责：
 *   在“机器整数”与“每字节存放两位十进制数”的压缩 BCD 表示之间转换，
 *   常用于 RTC 芯片寄存器、电表/水表、Modbus 寄存器等协议。
 *
 * 数据流与依赖：
 *   纯计算模块，无全局状态、无动态内存，仅依赖 标准头文件。
 *   调用者负责提供数据缓冲区并保证其长度。
 *
 * 边界约束：
 *   - 每字节高半字节为十位数字，低半字节为个位数字；半字节 > 9 视为非法。
 *   - 编码按右对齐写入：最低位数字落在最后一个字节的低半字节，高位补 0，
 *     输出长度恒为 output_len 个字节；十进制位数超过 output_len*2 返回 -1。
 *   - 解码不做溢出检查，输入过长时结果会按 uint32_t 回绕。
 */

#include "bcd.h"

/*
 * 校验一段数据是否为合法压缩 BCD（每个半字节都必须 <= 9）。
 * 参数：data - 数据首地址；len - 字节数
 * 返回：0 合法；-1 存在非法半字节
 * 注意：data 为 NULL 且 len 为 0 时视为合法（空数据）。
 */
int bcd_is_valid(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    if (data == NULL) {
        return (len == 0) ? 0 : -1;
    }
    for (i = 0; i < len; i++) {
        /* 高、低半字节分别检查，任一大于 9 即为非法 BCD */
        if ((data[i] & 0x0Fu) > 9u || ((data[i] >> 4) & 0x0Fu) > 9u) {
            return -1;
        }
    }
    return 0;
}

/*
 * 将数值编码为压缩 BCD（右对齐，高位在前）。
 * 参数：
 *   value      - 待编码数值（十进制，最多 10 位）
 *   output     - 输出缓冲区，长度由 output_len 指定
 *   output_len - 输出字节数，决定可容纳的十进制位数（output_len * 2）
 * 返回：0 成功；-1 参数非法或位数超出缓冲区容量
 * 注意：输出缓冲会先整体清零；value = 0 时结果为全 0，且至少占 1 位。
 */
int bcd_encode(uint32_t value, uint8_t *output, uint32_t output_len)
{
    uint32_t total_nibbles;
    uint32_t digits = 0;
    uint32_t v;
    int pos;
    uint32_t i;

    if (output == NULL || output_len == 0) {
        return -1;
    }

    /* 统计十进制位数（do-while 保证 value = 0 时也计为 1 位） */
    v = value;
    do {
        digits++;
        v /= 10;
    } while (v != 0);

    total_nibbles = output_len * 2u;
    if (digits > total_nibbles) {
        return -1;   /* 缓冲区不足以容纳全部十进制位 */
    }

    /* 先清零，未使用的高位半字节保持 0 */
    for (i = 0; i < output_len; i++) {
        output[i] = 0x00;
    }

    v = value;
    pos = (int)total_nibbles - 1;   /* 最低有效数字放在最后一个半字节 */
    for (;;) {
        uint8_t d = (uint8_t)(v % 10);
        int byte_idx = pos / 2;
        int nib = pos % 2;   /* 0 = 高半字节, 1 = 低半字节 */
        /* 低位半字节可能在正整数位置，故用 |= 与已清的 0 合并 */
        if (nib == 0) {
            output[byte_idx] |= (uint8_t)(d << 4);
        } else {
            output[byte_idx] |= d;
        }
        v /= 10;
        if (v == 0) {
            break;
        }
        pos--;   /* 向高位移动一个半字节 */
    }

    return 0;
}

/*
 * 将压缩 BCD 解码为数值。
 * 参数：input - BCD 数据；input_len - 字节数；value - 输出数值（不可为 NULL）
 * 返回：0 成功；-1 参数非法或存在非法 BCD 半字节
 * 注意：按字节顺序由高位到低位拼接，输入过长会超出 uint32_t 表示范围。
 */
int bcd_decode(const uint8_t *input, uint32_t input_len, uint32_t *value)
{
    uint32_t v = 0;
    uint32_t i;

    if (input == NULL || value == NULL) {
        return -1;
    }
    for (i = 0; i < input_len; i++) {
        uint8_t hi = (uint8_t)((input[i] >> 4) & 0x0F);
        uint8_t lo = (uint8_t)(input[i] & 0x0F);
        if (hi > 9u || lo > 9u) {
            return -1;   /* 非法 BCD，拒绝解码 */
        }
        /* 先拼十位再拼个位，保持十进制按位左移的效果 */
        v = v * 10u + hi;
        v = v * 10u + lo;
    }
    *value = v;
    return 0;
}
