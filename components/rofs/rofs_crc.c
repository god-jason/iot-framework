/*
 * rofs_crc.c - CRC32（CRC-32/ISO-HDLC，与 zlib.crc32 一致）
 * ============================================================================
 *
 * 多项式反射实现 0xEDB88320，初值/末值异或 0xFFFFFFFF。
 * 不建查找表，代码体积小，适合 MCU；镜像校验为一次性操作。
 */

#include "rofs_internal.h"

uint32_t rofs__crc32_update(uint32_t crc, const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t i;
    uint32_t j;

    for (i = 0u; i < len; i++) {
        crc ^= p[i];
        for (j = 0u; j < 8u; j++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint32_t rofs__crc32(const void *buf, uint32_t len)
{
    return rofs__crc32_update(0xFFFFFFFFu, buf, len) ^ 0xFFFFFFFFu;
}
