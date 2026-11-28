/*
 * crc32.c - CRC-32/IEEE 802.3 校验
 *
 * 逐字节、逐位计算，不占用查表空间；标准校验串 "123456789" -> 0xCBF43926。
 */

#include "crc32.h"

#include <stddef.h>

uint32_t crc32_update(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;
    uint8_t bit;

    if (data == NULL && len != 0U) {
        return 0U;
    }

    for (i = 0U; i < len; i++) {
        crc ^= (uint32_t)p[i];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ 0xEDB88320U;   /* 0x04C11DB7 的镜像 */
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint32_t crc32(const void *data, uint32_t len)
{
    if (data == NULL && len != 0U) {
        return 0U;
    }
    return crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}
