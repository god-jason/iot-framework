#ifndef CRC32_H
#define CRC32_H

/*
 * crc32.h - CRC-32/IEEE 802.3 校验
 * （poly 0x04C11DB7，init/xorout 0xFFFFFFFF，输入/输出反射）
 *
 * 无状态纯函数，可重入；len 为 0 时 data 可为 NULL。
 * 提供一次性与增量两种用法，供固件校验、KV/文件系统记录校验复用。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 增量计算：crc 初值传 0xFFFFFFFF，返回未做最终异或的中间值，
 * 可连续传入多段数据；data 非法（NULL 且 len != 0）时返回 0。
 */
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t len);

/* 一次性计算；data 非法（NULL 且 len != 0）时返回 0 */
uint32_t crc32(const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */
