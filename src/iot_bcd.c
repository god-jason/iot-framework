#include "iot_bcd.h"

int iot_bcd_is_valid(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    if (data == NULL) {
        return (len == 0) ? IOT_OK : -1;
    }
    for (i = 0; i < len; i++) {
        if ((data[i] & 0x0Fu) > 9u || ((data[i] >> 4) & 0x0Fu) > 9u) {
            return -1;
        }
    }
    return IOT_OK;
}

int iot_bcd_encode(uint32_t value, uint8_t *output, uint32_t output_len)
{
    uint32_t total_nibbles;
    uint32_t digits = 0;
    uint32_t v;
    int pos;
    uint32_t i;

    if (output == NULL || output_len == 0) {
        return -1;
    }

    /* 统计十进制位数 */
    v = value;
    do {
        digits++;
        v /= 10;
    } while (v != 0);

    total_nibbles = output_len * 2u;
    if (digits > total_nibbles) {
        return -1;
    }

    for (i = 0; i < output_len; i++) {
        output[i] = 0x00;
    }

    v = value;
    pos = (int)total_nibbles - 1;   /* 最低有效数字放在最后一个半字节 */
    for (;;) {
        uint8_t d = (uint8_t)(v % 10);
        int byte_idx = pos / 2;
        int nib = pos % 2;   /* 0 = 高半字节, 1 = 低半字节 */
        if (nib == 0) {
            output[byte_idx] |= (uint8_t)(d << 4);
        } else {
            output[byte_idx] |= d;
        }
        v /= 10;
        if (v == 0) {
            break;
        }
        pos--;
    }

    return IOT_OK;
}

int iot_bcd_decode(const uint8_t *input, uint32_t input_len, uint32_t *value)
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
            return -1;
        }
        v = v * 10u + hi;
        v = v * 10u + lo;
    }
    *value = v;
    return IOT_OK;
}