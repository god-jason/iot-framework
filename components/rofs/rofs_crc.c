/*
 * rofs_crc.c - CRC32 校验
 * ============================================================================
 *
 * ROFS 对三类数据分别计算 CRC32：
 *   1. Superblock 前 20B；
 *   2. Block Header 前 12B；
 *   3. Record Header 前 28B 与整个 Payload。
 *
 * 采用 IEEE 802.3 反射多项式 0xEDB88320，与常见 CRC32 工具一致。
 * 为满足 MCU 的 RAM 预算，这里不建 256 项查找表，而是逐位计算：
 *   - 代码量约一百多字节；
 *   - 速度较慢，但 ROFS 的元数据量很小，瓶颈通常在 SPI Flash。
 */

#include "rofs_internal.h"

/*
 * 以运行值方式更新 CRC。
 * crc 首次传 0xFFFFFFFF，后续把上次返回值继续传入；
 * 全部数据处理完后，调用者需要再异或 0xFFFFFFFF 得到最终值。
 */
uint32_t rofs__crc32_update(uint32_t crc, const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t i;
    uint32_t j;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        /* 逐位处理一个字节的 8 个 bit */
        for (j = 0; j < 8u; j++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* 一次性计算一段连续内存的最终 CRC32 */
uint32_t rofs__crc32(const void *buf, uint32_t len)
{
    return rofs__crc32_update(0xFFFFFFFFu, buf, len) ^ 0xFFFFFFFFu;
}
