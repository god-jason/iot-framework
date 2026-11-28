/*
 * crc8.c - CRC-8 校验（poly 0x07，init 0x00，不反射，xorout 0x00）
 *
 * 逐字节、逐位计算，不占用查表空间；标准校验串 "123456789" -> 0xF4。
 */

#include "crc8.h"

#include <stddef.h>

uint8_t crc8(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint8_t crc = 0x00U;
    uint32_t i;
    uint8_t bit;

    if (data == NULL && len != 0U) {
        return 0U;
    }

    for (i = 0U; i < len; i++) {
        crc ^= p[i];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((crc << 1) ^ 0x07U);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}
